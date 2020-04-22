#ifndef MAPLAB_SERVER_NODE_MAPLAB_SERVER_NODE_H_
#define MAPLAB_SERVER_NODE_MAPLAB_SERVER_NODE_H_

#include <atomic>
#include <deque>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <aslam/common/thread-pool.h>
#include <map-manager/map-manager.h>
#include <maplab-console/maplab-console.h>
#include <vi-map/vi-map.h>
#include <visualization/resource-visualization.h>
#include <visualization/viwls-graph-plotter.h>

#include "maplab-server-node/maplab-server-config.h"

namespace maplab {

struct SubmapProcess {
  // Name of the agent
  std::string robot_name;

  // Path to the map on the file system.
  std::string path;

  // Is true if the map has been loaded into the map manager already.
  bool is_loaded = false;

  // Map key of the map in the map manager.
  std::string map_key;

  // A unique hash to allow for quick lookup when multiple processes need to be
  // kept track of. Used by the server node to inform the user which maplab
  // console command is run on each SubmapProcess
  size_t map_hash;

  // Is true if the map has been processed, i.e. all the submap commands have
  // been applied to the map.
  bool is_processed = false;

  // Is true if submap has been merged into global map.
  bool is_merged = false;

  mutable std::mutex mutex;
};

class MaplabServerNode final {
 public:
  explicit MaplabServerNode(const MaplabServerNodeConfig& config);

  ~MaplabServerNode();

  // Once the node is started, the configuration cannot be changed anymore.
  void start();
  void shutdown();

  bool loadAndProcessSubmap(
      const std::string& robot_name, const std::string& submap_path);

  // Save the map to disk.
  bool saveMap(const std::string& path);
  bool saveMap();

  enum class MapLookupStatus : int {
    kSuccess = 0,
    kNoSuchMission = 1,
    kNoSuchSensor = 2,
    kPoseNotAvailableYet = 3,
    kPoseNeverAvailable = 4
  };
  MapLookupStatus mapLookup(
      const std::string& robot_name, const vi_map::SensorType sensor_type,
      const int64_t timestamp_ns, const Eigen::Vector3d& p_S,
      Eigen::Vector3d* p_G, Eigen::Vector3d* sensor_p_G) const;

  // Initially blacklists the mission, the merging thread will then remove it
  // within one iteration. All new submaps of this mission that arrive will be
  // discarded. The mission can be identified with a partial hash of length 4 or
  // more.
  bool deleteMission(
      const std::string& partial_mission_id_string,
      std::string* status_message);

  // Initially blacklists the missions of this robot, the merging thread will
  // then remove them within one iteration. All new submaps of these missions
  // that arrive will be discarded.
  bool deleteAllRobotMissions(
      const std::string& robot_name, std::string* status_message);

  void visualizeMap();

  void registerPoseCorrectionPublisherCallback(
      std::function<void(
          const int64_t, const std::string&, const aslam::Transformation&,
          const aslam::Transformation&, const aslam::Transformation&,
          const aslam::Transformation&)>
          callback);

  void registerStatusCallback(std::function<void(const std::string&)> callback);

 private:
  // Status thread functions:
  void printAndPublishServerStatus();

  // Submap processing functions:
  void extractLatestUnoptimizedPoseFromSubmap(
      const SubmapProcess& submap_process);
  void runSubmapProcessingCommands(const SubmapProcess& submap_process);

  // Map merging function:

  // Deletes missions from the merged map that have been blacklisted. Returns
  // false if no missions are left in the merged map, which also deletes it from
  // the map manager.
  bool deleteBlacklistedMissions();

  bool appendAvailableSubmaps();

  void saveMapEveryInterval();

  void runOneIterationOfMapMergingCommands();

  void publishDenseMap();

  void publishMostRecentVertexPoseAndCorrection();

  bool isSubmapBlacklisted(const std::string& map_key);

  const std::string kMergedMapKey = "merged_map";
  const int kSecondsToSleepBetweenAttempts = 1;
  const int kSecondsToSleepBetweenStatus = 1;

  MaplabServerNodeConfig config_;

  vi_map::VIMapManager map_manager_;

  std::thread submap_merging_thread_;
  std::thread status_thread_;
  std::function<void(const std::string&)> status_publisher_callback_;

  aslam::ThreadPool submap_loading_thread_pool_;

  std::mutex submap_processing_queue_mutex_;
  std::deque<SubmapProcess> submap_processing_queue_;

  MapLabConsole base_console_;
  std::unique_ptr<visualization::ViwlsGraphRvizPlotter> plotter_;

  bool is_running_;

  std::atomic<bool> shut_down_requested_;
  std::atomic<bool> merging_thread_busy_;

  mutable std::mutex mutex_;

  std::mutex submap_commands_mutex_;
  std::map<size_t, std::string> submap_commands_;

  std::mutex current_merge_command_mutex_;
  std::string current_merge_command_;

  double time_of_last_map_backup_s_;

  std::atomic<double> duration_last_merging_loop_s_;

  std::function<void(
      const int64_t, const std::string&, const aslam::Transformation&,
      const aslam::Transformation&, const aslam::Transformation&,
      const aslam::Transformation&)>
      pose_correction_publisher_callback_;

  struct RobotMissionInformation {
    // Contains the mission ids of this robot, the most recent mission is at the
    // front of the vector.
    std::list<vi_map::MissionId> mission_ids;

    // These keep track of the end/start poses of submaps as they came in
    // and the most recent submap end pose in the optimized map. This is used
    // to compute the correction T_B_old_B_new that is published by the
    // server. This correction can then be used to coorect any poses that were
    // expressed in the odometry frame that was used to build the map
    // initially.
    std::map<int64_t, aslam::Transformation> T_M_B_submaps_input;
    std::map<int64_t, aslam::Transformation> T_G_M_submaps_input;
  };

  mutable std::mutex robot_to_mission_id_map_mutex_;
  std::unordered_map<std::string, RobotMissionInformation>
      robot_to_mission_id_map_;
  std::unordered_map<vi_map::MissionId, std::string> mission_id_to_robot_map_;

  mutable std::mutex blacklisted_missions_mutex_;
  std::unordered_map<vi_map::MissionId, std::string> blacklisted_missions_;
};

}  // namespace maplab

#endif  // MAPLAB_SERVER_NODE_MAPLAB_SERVER_NODE_H_
