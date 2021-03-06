#include <cloud_map_merge/cloud_map_merge.h>
#include <boost/thread.hpp>
#include <exploration_libraly/struct.h>
#include <exploration_libraly/construct.h>
#include <forward_list>
#include <geometry_msgs/Pose2D.h>
#include <iterator>
#include <mutex>
#include <pcl_ros/point_cloud.h>
#include <ros/ros.h>
#include <sensor_msgs/PointCloud2.h>
#include <thread>

CloudMapMerge::CloudMapMerge():p_("~"),pc2_("merge_map",1,true){
    p_.param<std::string>("map_topic",MAP_TOPIC,"/rtabmap/cloud_obstacles");
    p_.param<std::string>("merge_map_frame",MERGE_MAP_FRAME,"merge_map");
    p_.param<std::string>("param_namespace",PARAM_NAMESPACE,"map_merge");
    p_.param<double>("ceiling_height",CEILING_HEIGHT,2.4);
    p_.param<double>("floor_height",FLOOR_HEIGHT,-0.05);
    p_.param<double>("ragistration_rate", RAGISTRATION_RATE, 0.5);
    p_.param<double>("merging_rate", MERGING_RATE, 1.0);
}

void CloudMapMerge::robotRegistration(void){
    //masterに登録されているtopicを読み込み
    ros::master::V_TopicInfo topicList;
    ros::master::getTopics(topicList);
    ROS_INFO_STREAM("registrationThread << robotRegistration\n");
    //topicListの中からmapのトピックのみを抽出
    for(const auto& topic : topicList){
        //maptopicであるか確認
        if(!isMapTopic(topic)){
            continue;
        }
        
        std::string robotName = robotNameFromTopicName(topic.name);
        //すでに登録されていないかロボットの名前を確認
        {
            bool isRegisterd = false;
            {
                boost::shared_lock<boost::shared_mutex> bLock(robotListMutex_);
                for(auto&& robot : robotList_){
                    std::lock_guard<std::mutex> lock(robot.mutex);
                    if(robotName == robot.name){
                        isRegisterd = true;
                    }
                }
            }
            if(isRegisterd){
                continue;
            }
        }
        //登録されていない場合だけ以下で登録を行う
        ROS_DEBUG_STREAM("registrationThread << add to system : " << robotName << "\n");
        {
            std::lock_guard<boost::shared_mutex> bLock(robotListMutex_);
            robotList_.emplace_front();
            CloudMapMerge::robotInfo& robot = robotList_.front();
            {
                ROS_DEBUG_STREAM("registrationThread << edit list\n");
                std::lock_guard<std::mutex> lock(robot.mutex);
                robot.name = robotName;
                initPoseLoad(robot);
                robot.sub = s_.subscribe<sensor_msgs::PointCloud2>(robot.name+MAP_TOPIC, 1, [this, &robot](const sensor_msgs::PointCloud2::ConstPtr& msg) {mapUpdate(msg, robot);});
                robot.initialized = false;
                robot.update = false;
            }
        }
    }
}

bool CloudMapMerge::isMapTopic(const ros::master::TopicInfo& topic){
    bool isMap = topic.name == ros::names::append(robotNameFromTopicName(topic.name),MAP_TOPIC);
    bool isPointCloud = topic.datatype == "sensor_msgs/PointCloud2";
    return isMap && isPointCloud;
}

std::string CloudMapMerge::robotNameFromTopicName(const std::string& topicName){
    //ros::names::parentNamespace(std::string);はトピック名からネームスペースの文字列を返す
    //例
    ///robot1/map -> /robot1
    ///robot1/abc/map -> /robot1/abc
    return ros::names::parentNamespace(ros::names::parentNamespace(topicName));
}

void CloudMapMerge::initPoseLoad(CloudMapMerge::robotInfo& robot){
    std::string ns = ros::names::append(robot.name,PARAM_NAMESPACE);
    p_.param<double>(ros::names::append(ns,"init_pose_x"), robot.initPose.x, 0.0);
    p_.param<double>(ros::names::append(ns,"init_pose_y"), robot.initPose.y, 0.0);
    p_.param<double>(ros::names::append(ns,"init_pose_yaw"), robot.initPose.theta, 0.0);
}

void CloudMapMerge::mapUpdate(const sensor_msgs::PointCloud2::ConstPtr& msg, CloudMapMerge::robotInfo& robot){
    //timestampが進んでいたらsensor_msgを更新してpclに変換する
    ROS_INFO_STREAM("suscribeThread << mapUpdate\n");
    std::lock_guard<std::mutex> lock(robot.mutex);
    if(robot.initialized && !(msg -> header.stamp > robot.rosMap.header.stamp)){
        return;
    }
    if(!robot.initialized){
        robot.pclMap = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(new pcl::PointCloud<pcl::PointXYZRGB>);
        robot.initialized = true;
    }
    robot.rosMap = *msg;
    pcl::fromROSMsg(*msg, *robot.pclMap);
    robot.update = true;
}

void CloudMapMerge::mapMerging(void){
    ROS_INFO_STREAM("mergingThread << mapMerging\n");
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr mergeCloud(new pcl::PointCloud<pcl::PointXYZRGB>);
    //updateされているかどうかで処理を変える
    {
        boost::shared_lock<boost::shared_mutex> lock(robotListMutex_);
        for(auto&& robot : robotList_){
            if(!robot.initialized){
                continue;
            }
            std::lock_guard<std::mutex> lock(robot.mutex);
            if(robot.update){
                pcl::PointCloud<pcl::PointXYZRGB>::Ptr tempCloud(new pcl::PointCloud<pcl::PointXYZRGB>);
                Eigen::Matrix2d rotation = ExpLib::Construct::eigenMat2d(cos(robot.initPose.theta), -sin(robot.initPose.theta), sin(robot.initPose.theta), cos(robot.initPose.theta));
                // rotation << cos(robot.initPose.theta) , -sin(robot.initPose.theta) , sin(robot.initPose.theta) , cos(robot.initPose.theta);
                for(const auto& point : robot.pclMap->points){
                    if(point.z < CEILING_HEIGHT && point.z > FLOOR_HEIGHT){
                        Eigen::Vector2d tempPoint(rotation * Eigen::Vector2d(point.x,point.y));
                        tempCloud -> points.emplace_back(ExpLib::Construct::pclXYZRGB(tempPoint.x() + robot.initPose.x, tempPoint.y() + robot.initPose.y,point.z,point.r,point.g,point.b));
                    }
                }
                robot.processedPclMap = std::move(tempCloud);
                robot.update = false;
            }
            *mergeCloud += *robot.processedPclMap;
        }
    }
    
    if(!mergeCloud -> points.empty()){
        ROS_DEBUG_STREAM("mergingThread << publish mergeCloud\n");
        mergeCloud -> width = mergeCloud -> points.size();
        mergeCloud -> height = 1;
        mergeCloud -> is_dense = false;

        sensor_msgs::PointCloud2 msg;
        pcl::toROSMsg (*mergeCloud, msg);
        msg.header.stamp = ros::Time::now();
        msg.header.frame_id = MERGE_MAP_FRAME;
        pc2_.pub.publish(msg);
    }    
}

void CloudMapMerge::registrationLoop(void){
    ROS_INFO_STREAM("start registration loop\n");
    ros::Rate rate(RAGISTRATION_RATE);
    while(ros::ok()){
        robotRegistration();
        rate.sleep();
    }
}

void CloudMapMerge::mergingLoop(void){
    ROS_INFO_STREAM("start merging loop\n");
    ros::Rate rate(MERGING_RATE);
    while(ros::ok()){
        mapMerging();
        rate.sleep();
    }
}

void CloudMapMerge::multiThreadMainLoop(void){
    ROS_INFO_STREAM("start threads\n");
    ros::spinOnce();
    std::thread mergingThread([this]() { mergingLoop(); });
    std::thread registrationThread([this]() { registrationLoop(); });
    ros::spin();
    mergingThread.join();//スレッドの終了を待つ??
    registrationThread.join();
    ROS_INFO_STREAM("end main loop\n");
}
