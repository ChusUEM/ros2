// Copyright (c) 2020 Fetullah Atas
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string>
#include <vector>
#include "nav2_gps_waypoint_follower_demo/gps_waypoint_follower_demo.hpp"

namespace nav2_gps_waypoint_follower_demo
{
GPSWayPointFollowerClient::GPSWayPointFollowerClient()
: Node("GPSWaypointFollowerClient"), goal_done_(false)
{
  gps_waypoint_follower_action_client_ =
    rclcpp_action::create_client<ClientT>(
    this->get_node_base_interface(),
    this->get_node_graph_interface(),
    this->get_node_logging_interface(),
    this->get_node_waitables_interface(),
    "FollowGPSWaypoints");
  this->timer_ = this->create_wall_timer(
    std::chrono::milliseconds(500),
    std::bind(&GPSWayPointFollowerClient::startWaypointFollowing, this));
  // number of poses that robot will go throug, specified in yaml file
  this->declare_parameter("number_of_gps_waypoints");
  int number_of_gps_waypoints =
    this->get_parameter("number_of_gps_waypoints").as_int();
  std::string gps_waypoint_name_prefix = "gps_waypoint";
  gps_waypoints_from_yaml_ = loadGPSWaypointsFromYAML(
    gps_waypoint_name_prefix,
    number_of_gps_waypoints);
  RCLCPP_INFO(
    this->get_logger(),
    "Loaded %i GPS waypoints from YAML, gonna pass them to FollowGPSWaypoints...",
    gps_waypoints_from_yaml_.size());
  RCLCPP_INFO(
    this->get_logger(),
    "Created an Instance of GPSWayPointFollowerClient");
}

GPSWayPointFollowerClient::~GPSWayPointFollowerClient()
{
  RCLCPP_INFO(
    this->get_logger(),
    "Destroyed an Instance of GPSWayPointFollowerClient");
}

void GPSWayPointFollowerClient::startWaypointFollowing()
{
  using namespace std::placeholders;
  this->timer_->cancel();
  this->goal_done_ = false;

  if (!this->gps_waypoint_follower_action_client_) {
    RCLCPP_ERROR(this->get_logger(), "Action client not initialized");
  }

  auto is_action_server_ready =
    gps_waypoint_follower_action_client_->wait_for_action_server(
    std::chrono::seconds(5));
  if (!is_action_server_ready) {
    RCLCPP_ERROR(
      this->get_logger(), "FollowGPSWaypoints action server is not available."
      " Make sure an instance of GPSWaypointFollower is up and running");
    this->goal_done_ = true;
    return;
  }
  gps_waypoint_follower_goal_ = ClientT::Goal();
  // Send the goal poses
  gps_waypoint_follower_goal_.waypoints = gps_waypoints_from_yaml_;

  RCLCPP_INFO(
    this->get_logger(),
    "Sending a path of %zu waypoints:", gps_waypoint_follower_goal_.waypoints.size());
  for (auto waypoint : gps_waypoint_follower_goal_.waypoints) {
    RCLCPP_DEBUG(
      this->get_logger(),
      "\t(%lf, %lf)", waypoint.latitude, waypoint.longitude);
  }

  auto goal_options =
    rclcpp_action::Client<ClientT>::SendGoalOptions();

  goal_options.goal_response_callback = std::bind(
    &GPSWayPointFollowerClient::goalResponseCallback, this, _1);

  goal_options.feedback_callback =
    std::bind(&GPSWayPointFollowerClient::feedbackCallback, this, _1, _2);

  goal_options.result_callback = std::bind(
    &GPSWayPointFollowerClient::resultCallback, this, _1);

  auto future_goal_handle = gps_waypoint_follower_action_client_->async_send_goal(
    gps_waypoint_follower_goal_, goal_options);

}

std::vector<sensor_msgs::msg::NavSatFix>
GPSWayPointFollowerClient::loadGPSWaypointsFromYAML(
  std::string waypoint_name_prefix, int num_waypoints)
{
  std::vector<sensor_msgs::msg::NavSatFix> gps_waypoint_msg_vector;
  // get all the poses in yaml file using provided utility function
  for (int i = 0; i < num_waypoints; i++) {
    // prefix for each fake task, the last letter is basically index
    std::string curr_waypoint_name = waypoint_name_prefix + std::to_string(i);
    try {
      this->declare_parameter(curr_waypoint_name);
      std::vector<double> gps_waypoint_vector =
        this->get_parameter(curr_waypoint_name).as_double_array();

      // throw exeption if incorrect format was detected from yaml file reading
      if (gps_waypoint_vector.size() < 3) {
        RCLCPP_FATAL(
          this->get_logger(),
          "GPS waypoint that was loaded from YAML file seems to have incorrect"
          "form, the right format is; Lat, Long, Alt with doble types");
        throw rclcpp::exceptions::InvalidParametersException(
                "[ERROR] See above error, correct "
                " the right format is; Lat, Long, Alt with doble types"
                "E.g gps_waypoint0; [0.0, 0.0, 0.0], please chechk YAML file");
      }
      // construct the gps waypoint and push them to pair
      // lat, long , alt
      sensor_msgs::msg::NavSatFix gps_point;
      gps_point.latitude = gps_waypoint_vector.at(0);
      gps_point.longitude = gps_waypoint_vector.at(1);
      gps_point.altitude = gps_waypoint_vector.at(2);
      gps_waypoint_msg_vector.push_back(gps_point);
    } catch (const std::exception & e) {
      std::cerr << e.what() << '\n';
    }
  }

  // return the read pair of this gps waypoint to it's caller
  return gps_waypoint_msg_vector;
}

void GPSWayPointFollowerClient::goalResponseCallback(
  GPSWaypointFollowerGoalHandle::SharedPtr goal_handle)
{
  if (!goal_handle) {
    RCLCPP_ERROR(this->get_logger(), "Goal was rejected by server");
  } else {
    RCLCPP_INFO(this->get_logger(), "Goal accepted by server, waiting for result");
  }
}

void GPSWayPointFollowerClient::feedbackCallback(
  GPSWaypointFollowerGoalHandle::SharedPtr,
  const std::shared_ptr<const ClientT::Feedback> feedback)
{
  RCLCPP_INFO(
    this->get_logger(),
    "Current waypoint: %i", feedback->current_waypoint);
}

void GPSWayPointFollowerClient::resultCallback(
  const GPSWaypointFollowerGoalHandle::WrappedResult & result)
{
  this->goal_done_ = true;
  switch (result.code) {
    case rclcpp_action::ResultCode::SUCCEEDED:
      break;
    case rclcpp_action::ResultCode::ABORTED:
      RCLCPP_ERROR(this->get_logger(), "Goal was aborted");
      return;
    case rclcpp_action::ResultCode::CANCELED:
      RCLCPP_ERROR(this->get_logger(), "Goal was canceled");
      return;
    default:
      RCLCPP_ERROR(this->get_logger(), "Unknown result code");
      return;
  }

  RCLCPP_INFO(this->get_logger(), "Result received");
  for (auto number : result.result->missed_waypoints) {
    RCLCPP_INFO(this->get_logger(), "Missed Waypoint %i", number);
  }
}

bool GPSWayPointFollowerClient::is_goal_done() const
{
  return this->goal_done_;
}

}  // namespace nav2_gps_waypoint_follower_demo

/**
 * @brief Entry point for Way Point following demo Node
 *
 * @param argc
 * @param argv
 * @return int
 */
int main(int argc, char const * argv[])
{
  rclcpp::init(argc, argv);
  auto gps_waypoint_follower_client_node = std::make_shared
    <nav2_gps_waypoint_follower_demo::GPSWayPointFollowerClient>();

  while (!gps_waypoint_follower_client_node->is_goal_done()) {
    rclcpp::spin_some(gps_waypoint_follower_client_node);
  }
  rclcpp::shutdown();
  return 0;

}
