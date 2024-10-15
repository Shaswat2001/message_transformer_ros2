#include <errno.h>
#include "geometry_msgs/msg/quaternion.hpp"
#include <geometry_msgs/msg/twist_stamped.hpp>
#include <geometry_msgs/msg/pose_with_covariance_stamped.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <std_msgs/msg/float64.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <sensor_msgs/msg/joint_state.hpp>
#include <rclcpp/rclcpp.hpp>
#include <tf2/transform_datatypes.h>
#include <tf2_ros/transform_broadcaster.h>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <time.h>
#include <unistd.h>
#include <tf2/LinearMath/Quaternion.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.hpp>
#include "message_transformer/protocol.h"

using namespace std;
#define PI 3.1415926

class QNX2ROS{

    public:
        QNX2ROS(rclcpp::Node::SharedPtr node, int server_port) : node_(node), odom_broadcaster_(node)
        {
            socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
            if (socket_fd_ < 0)
            {
            perror("socket");
            exit(1);
            }

            memset(&listen_addr_, 0, sizeof(struct sockaddr_in));        ///< initialize to zeros
            listen_addr_.sin_family = AF_INET;                           ///< host byte order
            listen_addr_.sin_port = htons(server_port);                    ///< port in network byte order
            listen_addr_.sin_addr.s_addr = htonl(INADDR_ANY);            ///< automatically fill in my IP
            if (bind(socket_fd_, (struct sockaddr *)&listen_addr_, sizeof(listen_addr_)) < 0) 
            {
            perror("bind error:");
            exit(1);
            }

            leg_odom_pub_ = node_->create_publisher<geometry_msgs::msg::PoseWithCovarianceStamped>("leg_odom", 1);
            leg_odom_pub2_ = node_->create_publisher<nav_msgs::msg::Odometry>("leg_odom2", 1);
            joint_state_pub_ = node_->create_publisher<sensor_msgs::msg::JointState>("joint_states", 1);
            imu_pub_200_ = node_->create_publisher<sensor_msgs::msg::Imu>("/imu/data", 1);
            handle_pub_ = node_->create_publisher<geometry_msgs::msg::Twist>("/handle_state", 1);
            ultrasound_pub_ = node_->create_publisher<std_msgs::msg::Float64>("/us_publisher/ultrasound_distance", 1);

        }

        ~QNX2ROS()
        {
            close(socket_fd_);
        }

        void ReceiveFrame()
        {
            int len = sizeof(listen_addr_);
            recv_num_ = recvfrom(socket_fd_, receive_buffer_, sizeof(receive_buffer_), 0, (struct sockaddr *)&listen_addr_, (socklen_t *)&len);
            if (recv_num_ < 0)
            {
                perror("recvfrom error:");
                exit(1);
            }

            times_++;
            if (times_ == 3000)
            {
                RCLCPP_INFO(node_->get_logger(), "qnx2ros received: [%d] bytes", recv_num_);
                times_ = 0;
            }
        }

        void ParseFrame()
        {
            switch (recv_num_)
            {
            case sizeof(QNX2ROSProtocol::RobotStateReceived):
                ParseRobotStateFrame();
                break;

            case sizeof(QNX2ROSProtocol::JointStateReceived):
                ParseJointStateFrame();
                break;

            case sizeof(QNX2ROSProtocol::HandleStateReceived):
                ParseHandleStateFrame();
                break;

            case sizeof(QNX2ROSProtocol::ImuDataReceived):
                ParseImuDataFrame();
                break;

            default:
                RCLCPP_WARN(node_->get_logger(), "UDP packet length not matching expected frames");
                break;
            }
        }

        void ParseRobotStateFrame()
        {
            QNX2ROSProtocol::RobotStateReceived *dr = (QNX2ROSProtocol::RobotStateReceived *)(receive_buffer_);
            QNX2ROSProtocol::RobotState *robot_state = &dr->data;

            if (dr->code == 2305)
            {
                geometry_msgs::msg::PoseWithCovarianceStamped leg_odom_data;
                leg_odom_data.header.frame_id = "odom";
                leg_odom_data.header.stamp = node_->get_clock()->now();
                tf2::Quaternion q1;
                q1.setRPY(0, 0, robot_state->rpy[2] / 180 * PI);
                leg_odom_data.pose.pose.orientation = tf2::toMsg(q1);
                leg_odom_data.pose.pose.position.x = robot_state->pos_world[0];
                leg_odom_data.pose.pose.position.y = robot_state->pos_world[1];
                leg_odom_data.pose.pose.position.z = robot_state->pos_world[2];
                leg_odom_pub_->publish(leg_odom_data);

                nav_msgs::msg::Odometry leg_odom_data2;
                leg_odom_data2.header.stamp = node_->get_clock()->now();
                leg_odom_data2.pose = leg_odom_data.pose;
                leg_odom_data2.twist.twist.linear.x = robot_state->vel_body[0];
                leg_odom_data2.twist.twist.linear.y = robot_state->vel_body[1];
                leg_odom_data2.twist.twist.angular.z = robot_state->rpy_vel[2];
                leg_odom_pub2_->publish(leg_odom_data2);

                sensor_msgs::msg::Imu imu_msg;
                imu_msg.header.frame_id = "imu";
                imu_msg.header.stamp = node_->get_clock()->now();
                tf2::Quaternion q;
                q.setRPY(robot_state->rpy[0] / 180 * PI, robot_state->rpy[1] / 180 * PI, robot_state->rpy[2] / 180 * PI);
                imu_msg.orientation = tf2::toMsg(q);
                imu_msg.angular_velocity.x = robot_state->rpy_vel[0];
                imu_msg.angular_velocity.y = robot_state->rpy_vel[1];
                imu_msg.angular_velocity.z = robot_state->rpy_vel[2];
                imu_msg.linear_acceleration.x = robot_state->xyz_acc[0];
                imu_msg.linear_acceleration.y = robot_state->xyz_acc[1];
                imu_msg.linear_acceleration.z = robot_state->xyz_acc[2];
                imu_pub_200_->publish(imu_msg);

                std_msgs::msg::Float64 ultrasound_distance;
                ultrasound_distance.data = robot_state->ultrasound[1];
                ultrasound_pub_->publish(ultrasound_distance);

                geometry_msgs::msg::TransformStamped leg_odom_trans;
                leg_odom_trans.header.stamp = node_->get_clock()->now();
                leg_odom_trans.header.frame_id = "odom";
                leg_odom_trans.child_frame_id = "base_link";
                leg_odom_trans.transform.translation.x = leg_odom_data.pose.pose.position.x;
                leg_odom_trans.transform.translation.y = leg_odom_data.pose.pose.position.y;
                leg_odom_trans.transform.translation.z = leg_odom_data.pose.pose.position.z;
                leg_odom_trans.transform.rotation = imu_msg.orientation;
                odom_broadcaster_.sendTransform(leg_odom_trans);
            }
        }

        void ParseJointStateFrame()
        {
            QNX2ROSProtocol::JointStateReceived *dr = (QNX2ROSProtocol::JointStateReceived *)(receive_buffer_);
            QNX2ROSProtocol::JointState *joint_state = &dr->data;
            if (dr->code == 2306)
            {
                sensor_msgs::msg::JointState joint_state_data;
                joint_state_data.header.stamp = node_->get_clock()->now();
                joint_state_data.name.resize(12);
                joint_state_data.position.resize(12);

                joint_state_data.name[0] = "LF_Joint";
                joint_state_data.position[0] = -joint_state->LF_Joint;
                joint_state_data.name[1] = "LF_Joint_1";
                joint_state_data.position[1] = -joint_state->LF_Joint_1;
                joint_state_data.name[2] = "LF_Joint_2";
                joint_state_data.position[2] = -joint_state->LF_Joint_2;

                joint_state_data.name[3] = "RF_Joint";
                joint_state_data.position[3] = -joint_state->RF_Joint;
                joint_state_data.name[4] = "RF_Joint_1";
                joint_state_data.position[4] = -joint_state->RF_Joint_1;
                joint_state_data.name[5] = "RF_Joint_2";
                joint_state_data.position[5] = -joint_state->RF_Joint_2;

                joint_state_data.name[6] = "LB_Joint";
                joint_state_data.position[6] = -joint_state->LB_Joint;
                joint_state_data.name[7] = "LB_Joint_1";
                joint_state_data.position[7] = -joint_state->LB_Joint_1;
                joint_state_data.name[8] = "LB_Joint_2";
                joint_state_data.position[8] = -joint_state->LB_Joint_2;

                joint_state_data.name[9] = "RB_Joint";
                joint_state_data.position[9] = -joint_state->RB_Joint;
                joint_state_data.name[10] = "RB_Joint_1";
                joint_state_data.position[10] = -joint_state->RB_Joint_1;
                joint_state_data.name[11] = "RB_Joint_2";
                joint_state_data.position[11] = -joint_state->RB_Joint_2;

                joint_state_pub_->publish(joint_state_data);
            }
        }

        void ParseHandleStateFrame()
        {
            QNX2ROSProtocol::HandleStateReceived *dr = (QNX2ROSProtocol::HandleStateReceived *)(receive_buffer_);
            QNX2ROSProtocol::HandleState *handle_state = &dr->data;
            if (dr->code == 2309)
            {
                geometry_msgs::msg::Twist handle_state_msg;
                handle_state_msg.linear.x = handle_state->left_axis_forward;
                handle_state_msg.linear.y = handle_state->left_axis_side;
                handle_state_msg.angular.z = -handle_state->right_axis_yaw;
                handle_pub_->publish(handle_state_msg);
            }
        }

        void ParseImuDataFrame()
        {
            QNX2ROSProtocol::ImuDataReceived *dr = (QNX2ROSProtocol::ImuDataReceived *)(receive_buffer_);
            QNX2ROSProtocol::ImuData *imu_data = &dr->data;

            if (dr->code == 0x010901)
            {
                sensor_msgs::msg::Imu imu_msg;
                imu_msg.header.frame_id = "base_link";
                imu_msg.header.stamp = node_->get_clock()->now();
                tf2::Quaternion q;
                q.setRPY(imu_data->angle_roll / 180 * PI,
                        imu_data->angle_pitch / 180 * PI,
                        imu_data->angle_yaw / 180 * PI);
                imu_msg.orientation = tf2::toMsg(q);
                imu_msg.angular_velocity.x = imu_data->angular_velocity_roll;
                imu_msg.angular_velocity.y = imu_data->angular_velocity_pitch;
                imu_msg.angular_velocity.z = imu_data->angular_velocity_yaw;
                imu_msg.linear_acceleration.x = imu_data->acc_x;
                imu_msg.linear_acceleration.y = imu_data->acc_y;
                imu_msg.linear_acceleration.z = imu_data->acc_z;
                imu_pub_200_->publish(imu_msg);
            }
        }

        void RunRoutine()
        {
            ReceiveFrame();
            ParseFrame();
        }

    private:
        int socket_fd_=-1;
        struct sockaddr_in listen_addr_;
        int recv_num_ = -1;
        uint8_t receive_buffer_[512];
        int times_=0;

        rclcpp::Node::SharedPtr node_;
        rclcpp::Publisher<geometry_msgs::msg::PoseWithCovarianceStamped>::SharedPtr leg_odom_pub_;
        rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr leg_odom_pub2_;
        rclcpp::Publisher<sensor_msgs::msg::JointState>::SharedPtr joint_state_pub_;
        rclcpp::Publisher<sensor_msgs::msg::Imu>::SharedPtr imu_pub_200_;
        rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr handle_pub_;
        rclcpp::Publisher<std_msgs::msg::Float64>::SharedPtr ultrasound_pub_;
        tf2_ros::TransformBroadcaster odom_broadcaster_;

};


int main(int argc, char * argv[])
{
    rclcpp::init(argc, argv);

    auto nh = std::make_shared<rclcpp::Node>("qnx2ros");
    
    int server_port;
    nh->declare_parameter("server_port", 43897); // Declare the parameter
    nh->get_parameter("server_port", server_port); // Get the parameter
    QNX2ROS qnx2ros(nh, server_port);
    rclcpp::Rate loop_rate(10000); // Set the loop rate

    while (rclcpp::ok()) {
        rclcpp::spin_some(nh); // Process incoming messages
        qnx2ros.RunRoutine();   // Call your method
        loop_rate.sleep();      // Sleep to maintain loop rate
    }

    rclcpp::shutdown();
    return 0;
}