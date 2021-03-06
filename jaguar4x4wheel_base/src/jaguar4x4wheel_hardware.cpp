/**
*
*  \author     Paul Bovbel <pbovbel@clearpathrobotics.com>
*  \copyright  Copyright (c) 2014-2015, Clearpath Robotics, Inc.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in the
*       documentation and/or other materials provided with the distribution.
*     * Neither the name of Clearpath Robotics, Inc. nor the
*       names of its contributors may be used to endorse or promote products
*       derived from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL CLEARPATH ROBOTICS, INC. BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* Please send comments, questions, or patches to code@clearpathrobotics.com
*
*/

#include "jaguar4x4wheel_base/jaguar4x4wheel_hardware.h"
#include <boost/assign/list_of.hpp>

using namespace DrRobot_MotionSensorDriver;

namespace {
  const float TICKS_PER_METER = 345; //345 based upon the diameter of the wheel including the track, 452 based upon the diamater of the wheel excluding the track. 345 works best inside the lab, 452 works best on the carpet outside
  const uint ENCODER_MIN = 0;
  const uint ENCODER_MAX = 32767;
  const uint PULSES_PER_REVOLUTION = 186;//190; // for speed encoder
}

namespace jaguar4x4wheel_base {

  /**
  * Initialize Jaguar4x4Wheel hardware
  */
  Jaguar4x4WheelHardware::Jaguar4x4WheelHardware(ros::NodeHandle nh, ros::NodeHandle private_nh, double target_control_freq)
      : nh_(nh),
        private_nh_(private_nh)
  {
    private_nh_.param<double>("wheel_diameter", wheel_diameter_, 0.27); // or 0.3555?
    private_nh_.param<double>("max_accel", max_accel_, 5.0);
    private_nh_.param<double>("max_speed", max_speed_, 2.0);
    private_nh_.param<double>("polling_timeout_", polling_timeout_, 10.0);

    std::string port;
    private_nh_.param<std::string>("port", port, "/dev/prolific");

    robot_config_.commMethod = DrRobot_MotionSensorDriver::Network;
    robot_config_.boardType = DrRobot_MotionSensorDriver::Jaguar;
    robot_config_.portNum = 10001;
    strncpy(robot_config_.robotIP, "172.16.51.52", sizeof(robot_config_.robotIP) - 1);
    drrobot_motion_driver_.setDrRobotMotionDriverConfig(&robot_config_);

    pid_controller_left_.init(ros::NodeHandle(private_nh_, "pid_parameters"));
    pid_controller_left_.reset();
    pid_controller_right_.init(ros::NodeHandle(private_nh_, "pid_parameters"));
    pid_controller_right_.reset();

    init_gains_ = pid_controller_left_.getGains();
    resetTravelOffset();
    registerControlInterfaces();

  }

  /**
  * Get current encoder travel offsets from MCU and bias future encoder readings against them
  */
  void Jaguar4x4WheelHardware::resetTravelOffset()
  {
    int res = drrobot_motion_driver_.openNetwork(robot_config_.robotIP,robot_config_.portNum);
    if (res == 0)
    {
      ROS_INFO("open port number at: [%d]", robot_config_.portNum);
    }
    else
    {
      ROS_ERROR("could not open network connection to [%s,%d]",  robot_config_.robotIP,robot_config_.portNum);
      ROS_ERROR("error code [%d]",  res);
    }
  }


  /**
  * Register interfaces with the RobotHW interface manager, allowing ros_control operation
  */
  void Jaguar4x4WheelHardware::registerControlInterfaces()
  {
    ros::V_string joint_names = boost::assign::list_of("rear_left_wheel")("rear_right_wheel")("front_left_wheel")
                                ("front_right_wheel");
    for (unsigned int i = 0; i < joint_names.size(); i++)
    {
      hardware_interface::JointStateHandle joint_state_handle(joint_names[i],
                                                              &joints_[i].position, &joints_[i].velocity, &joints_[i].effort);
      joint_state_interface_.registerHandle(joint_state_handle);

      hardware_interface::JointHandle joint_handle(
          joint_state_handle, &joints_[i].velocity_command);
      velocity_joint_interface_.registerHandle(joint_handle);
    }
    registerInterface(&joint_state_interface_);
    registerInterface(&velocity_joint_interface_);
  }

  /**
  * Pull latest speed and travel measurements from MCU, and store in joint structure for ros_control
  */
  void Jaguar4x4WheelHardware::updateJointsFromHardware()
  {
    if(drrobot_motion_driver_.portOpen())
    {
      drrobot_motion_driver_.readMotorSensorData(&motor_sensor_data_);

      // Translate from driver data to ROS data
      //motors used are 3 & 4
      //3 is left motor
      //4 is right motor
      for (uint j = 0 ; j < 4; ++j)
      {
        uint i = j;
        if(j > 1)
          i = j+1;
//        if( j == 3)
//          ROS_DEBUG_STREAM(" motorSensorEncoderPos[i] "<<motor_sensor_data_.motorSensorEncoderPos[j]
//                           <<" motorSensorEncoderVel[i] "<<motor_sensor_data_.motorSensorEncoderVel[j]
//                           <<" motorSensorEncoderDir[i] "<<motor_sensor_data_.motorSensorEncoderDir[j]);
        double delta = double(motor_sensor_data_.motorSensorEncoderPos[i])/double(PULSES_PER_REVOLUTION)*2.0*M_PI;
        if(i == 1 || i == 4)
          delta = -delta;
        delta += - joints_[j].position_offset - joints_[j].position;

        // detect suspiciously large readings, possibly from encoder rollover
        if (std::abs(delta) < 3.0)
        {
          joints_[j].position += delta;
        }
        else
        {
          // suspicious! drop this measurement and update the offset for subsequent readings
          joints_[j].position_offset += delta;
        }

        double velocity = double(motor_sensor_data_.motorSensorEncoderVel[i])/double(PULSES_PER_REVOLUTION)*2.0*M_PI;
        if(motor_sensor_data_.motorSensorEncoderDir[i] == 0)
          velocity = -velocity;

        joints_[j].velocity = velocity ;
        if(j == 2)
          ROS_DEBUG_STREAM(i<<" delta "<<angularToLinear(delta)<<" m velocity "<<angularToLinear(velocity)<<" m/s");
        if(j == 3)
          ROS_DEBUG_STREAM(i<<" delta "<<angularToLinear(delta)<<" m velocity "<<angularToLinear(velocity)<<" m/s");
      }
    }

  }

  /**
  * Get latest velocity commands from ros_control via joint structure, and send to MCU
  */
  void Jaguar4x4WheelHardware::writeCommandsToHardware(ros::Duration& dt)
  {
    double error_left = joints_[0].velocity_command - (joints_[0].velocity+joints_[2].velocity)/2;
    double error_right = joints_[1].velocity_command - (joints_[1].velocity+joints_[3].velocity)/2;

    control_toolbox::Pid::Gains new_gains_left = init_gains_;
    control_toolbox::Pid::Gains new_gains_right = init_gains_;
    if(fabs(joints_[0].velocity_command) < 0.5)
    {
      new_gains_left.i_gain_ = 0;
      pid_controller_left_.reset();
    }
    if(fabs(joints_[1].velocity_command) < 0.5)
    {
      new_gains_right.i_gain_ = 0;
      pid_controller_right_.reset();
    }

    if(fabs(error_left) < 0.1)
      new_gains_left.p_gain_ = 0;
    if(fabs(error_right) < 0.1)
      new_gains_right.p_gain_ = 0;

    pid_controller_left_.setGains(new_gains_left);
    pid_controller_right_.setGains(new_gains_right);

    double diff_speed_left = pid_controller_left_.computeCommand(error_left, dt);
    double diff_speed_right = pid_controller_right_.computeCommand(error_right, dt);
    control_toolbox::Pid::Gains gain = pid_controller_left_.getGains();
    ROS_DEBUG_STREAM("diff_speed_left_rear "<<diff_speed_left <<
                    " joints_[0].velocity "<<joints_[0].velocity<<
                    " joints_[0].velocity_command "<<joints_[0].velocity_command<<
                    " error_left "<<error_left<<
                    " i left gain "<<gain.i_gain_);
    diff_speed_left = angularToLinear(diff_speed_left);
    diff_speed_right = angularToLinear(diff_speed_right);

    // roue décollée
    // -1   => 3m/s
    // -0.8 => 2.2m/s
    // -0.5 => 1m/s
    // -0.2 => 0.3m/s
//    double diff_speed_left = angularToLinear(joints_[0].velocity_command);
//    double diff_speed_right = angularToLinear(joints_[1].velocity_command);

    limitDifferentialSpeed(diff_speed_left, diff_speed_right);

    double linear_speed = (diff_speed_left + diff_speed_right) * 0.5;
    double differential_speed = diff_speed_left - diff_speed_right;
    int forwardPWM = -linear_speed * 16384 + 16384;
    int turnPWM = differential_speed * 16384 + 16384;
    if (forwardPWM > ENCODER_MAX) forwardPWM = ENCODER_MAX;
    if (forwardPWM < 0) forwardPWM = 0;
    if (turnPWM > ENCODER_MAX) turnPWM = ENCODER_MAX;
    if (turnPWM < 0) turnPWM = 0;
    drrobot_motion_driver_.sendMotorCtrlAllCmd(PWM,NOCONTROL,NOCONTROL,NOCONTROL,forwardPWM,turnPWM, NOCONTROL);
  }

  /**
  * Scale left and right speed outputs to maintain ros_control's desired trajectory without saturating the outputs
  */
  void Jaguar4x4WheelHardware::limitDifferentialSpeed(double &diff_speed_left, double &diff_speed_right)
  {
    double large_speed = std::max(std::abs(diff_speed_left), std::abs(diff_speed_right));

    if (large_speed > max_speed_)
    {
      diff_speed_left *= max_speed_ / large_speed;
      diff_speed_right *= max_speed_ / large_speed;
    }
  }

  /**
  * Jaguar4x4Wheel reports travel in metres, need radians for ros_control RobotHW
  */
  double Jaguar4x4WheelHardware::linearToAngular(const double &travel) const
  {
    return travel / wheel_diameter_ * 2;
  }

  /**
  * RobotHW provides velocity command in rad/s, Jaguar4x4Wheel needs m/s,
  */
  double Jaguar4x4WheelHardware::angularToLinear(const double &angle) const
  {
    return angle * wheel_diameter_ / 2;
  }


}  // namespace jaguar4x4wheel_base
