#include <ros/ros.h>
#include <exploration_msgs/Frontier.h>
#include <geometry_msgs/Pose.h>
#include <geometry_msgs/Point.h>
#include <exploration/common_lib.hpp>
#include <exploration/path_planning.hpp>
#include <navfn/navfn_ros.h>

class SeamlessHybrid
{
private:
    struct sumValue{
        double sumDistnance;
        double sumAngle;
        geometry_msgs::Point coordinate;
    };

    struct maxValue{
        double distance;
        double angle;
        maxValue(double d, double a):distance(d),angle(a){};
    };

    std::string MAP_FRAME_ID;
	double THROUGH_TOLERANCE;
    double COVARIANCE_THRESHOLD;
    double VARIANCE_THRESHOLD;
    double ANGLE_WEIGHT;
    double NORM_WEIGHT;

    std::vector<CommonLib::listStruct> inputBranches;
    std::vector<exploration_msgs::Frontier> frontiers;
    geometry_msgs::Pose pose;

    std::vector<geometry_msgs::Point> branches;

    std::vector<sumValue> sVal;
    maxValue mVal;

    PathPlanning<navfn::NavfnROS>* pp_;
    
    static std::vector<geometry_msgs::Point> throughBranches; //一度重複探査を無視して行った座標（二回目は行けない）

public:
    SeamlessHybrid(const std::vector<CommonLib::listStruct>& b, const std::vector<exploration_msgs::Frontier>& f, const geometry_msgs::Pose& p, PathPlanning<navfn::NavfnROS>& pp);
    bool initialize(void);
    bool dataFilter(void);
    void evaluationInitialize(void);
    bool result(geometry_msgs::Point& goal);
};

std::vector<geometry_msgs::Point> SeamlessHybrid::throughBranches;

SeamlessHybrid::SeamlessHybrid(const std::vector<CommonLib::listStruct>& b, const std::vector<exploration_msgs::Frontier>& f, const geometry_msgs::Pose& p, PathPlanning<navfn::NavfnROS>& pp)
    :inputBranches(b)
    ,frontiers(f)
    ,pose(p)
    ,mVal(-DBL_MAX,-DBL_MAX){

    pp_ = &pp;
    ros::NodeHandle ph("~");
	ph.param<std::string>("map_frame_id", MAP_FRAME_ID, "map");
    ph.param<double>("through_tolerance", THROUGH_TOLERANCE, 1.0);
    ph.param<double>("covariance_threshold", COVARIANCE_THRESHOLD, 0.7);
    ph.param<double>("variance_threshold", VARIANCE_THRESHOLD, 1.5);
    ph.param<double>("angle_weight", ANGLE_WEIGHT, 1.5);
    ph.param<double>("norm_weight", NORM_WEIGHT, 2.5); 
}

bool SeamlessHybrid::initialize(void){
    if(!dataFilter()) {
        ROS_WARN_STREAM("initialize missing !!");
        return false;
    }
    evaluationInitialize();
    return true;
}

bool SeamlessHybrid::dataFilter(void){

    ROS_INFO_STREAM("before frontiers size : " << frontiers.size());
    auto eraseIndex = std::remove_if(frontiers.begin(), frontiers.end(),[this](exploration_msgs::Frontier& f){
        double v = f.variance.x>f.variance.y ? f.variance.x : f.variance.y;
        return v < VARIANCE_THRESHOLD && std::abs(f.covariance) < COVARIANCE_THRESHOLD;
    });
    frontiers.erase(eraseIndex,frontiers.end());

    ROS_INFO_STREAM("after frontiers size : " << frontiers.size());

    if(frontiers.size()==0) return false;

    ROS_INFO_STREAM("through branch size : " << throughBranches.size());

    ROS_INFO_STREAM("before branches size : " << inputBranches.size());

    branches.reserve(inputBranches.size());

    for(const auto& i : inputBranches){
        //duplication filter
        if(i.duplication == CommonLib::DuplicationStatus::NEWER){
            ROS_INFO_STREAM("newer duplication!!");
            continue;
        }
        //throught filter
        auto through = [&,this]{
            for(const auto& t : throughBranches) {
                if(Eigen::Vector2d(i.point.x-t.x,i.point.y-t.y).norm() < THROUGH_TOLERANCE) return true;
            }
            return false;
        };
        if(through()){
            ROS_INFO_STREAM("throught branch!!");
            continue;
        }
        branches.emplace_back(i.point);
    }
    //filteredListとfrontiers
    ROS_INFO_STREAM("after branches size : " << branches.size());

    return branches.size()==0 ? false : true;
}

void SeamlessHybrid::evaluationInitialize(void){
    // PathPlanning<navfn::NavfnROS> pp("global_costmap","NavfnROS");

    auto calc = [this](const geometry_msgs::Point& p, const Eigen::Vector2d& v1){
        ROS_DEBUG_STREAM("calc p : (" << p.x << "," << p.y << ")");
        ROS_DEBUG_STREAM("v1 : (" << v1[0] << "," << v1[1] << ")");
        sumValue s{0,0,p};
        for(const auto& f : frontiers){
            // 目標地点での向きをpathの最後の方の移動で決めたい
            Eigen::Vector2d v2;
            double distance;
            if(!pp_->getDistanceAndVec(CommonLib::pointToPoseStamped(p,MAP_FRAME_ID),CommonLib::pointToPoseStamped(f.coordinate,MAP_FRAME_ID),distance,v2)){
                v2 = Eigen::Vector2d(f.coordinate.x - p.x, f.coordinate.y - p.y).normalized();
                distance = pp_->getDistance(CommonLib::pointToPoseStamped(p,MAP_FRAME_ID),CommonLib::pointToPoseStamped(f.coordinate,MAP_FRAME_ID));
            }

            ROS_DEBUG_STREAM("v2 : (" << v2[0] << "," << v2[1] << ")");

            double angle = std::abs(acos(v1.dot(v2)));

            ROS_INFO_STREAM("distance to frontier[m]: " << distance);
            ROS_INFO_STREAM("angle to frontier[deg]: " << (angle*180)/M_PI);

            s.sumAngle += angle;
            s.sumDistnance += distance;

            if(angle > mVal.angle) mVal.angle = std::move(angle);
            if(distance > mVal.distance) mVal.distance = std::move(distance);
        }
        return s;
    };

    sVal.reserve(branches.size()+1);

    double forward = 0;
    for(const auto& b : branches){
        double distance;
        Eigen::Vector2d v1;
        if(!pp_->getDistanceAndVec(CommonLib::pointToPoseStamped(pose.position,MAP_FRAME_ID),CommonLib::pointToPoseStamped(b,MAP_FRAME_ID),distance,v1)){
            v1 = Eigen::Vector2d(b.x - pose.position.x, b.y - pose.position.y);
            // forward += v1.norm();
            distance = v1.lpNorm<1>();
            v1.normalize();
        }
        ROS_INFO_STREAM("distance to branch[m]: " << distance);
        sVal.emplace_back(calc(b,v1));
        forward += distance;
    }
    forward /= branches.size();

    ROS_INFO_STREAM("forward: " << forward);

    //直進時の計算
    double yaw = CommonLib::qToYaw(pose.orientation);
    double cosYaw = cos(yaw);
    double sinYaw = sin(yaw);

    ROS_INFO_STREAM("yaw: " << yaw << ", cos: " << cosYaw << ", sin:" << sinYaw);

    sVal.emplace_back(calc(CommonLib::msgPoint(pose.position.x+forward*cosYaw,pose.position.y+forward*sinYaw),Eigen::Vector2d(cosYaw,sinYaw)));
}

bool SeamlessHybrid::result(geometry_msgs::Point& goal){
    ROS_DEBUG_STREAM("sVal size : " << sVal.size());

    double minE = DBL_MAX;

    for(int i=0,ie=sVal.size();i!=ie;++i){
        double e = NORM_WEIGHT * sVal[i].sumDistnance / mVal.distance + ANGLE_WEIGHT * sVal[i].sumAngle / mVal.angle;
        ROS_DEBUG_STREAM("position : (" << sVal[i].coordinate.x << "," << sVal[i].coordinate.y << "), sum : " << e);
        if(e < minE){
            if(i == ie-1) return false;
            minE = std::move(e);
            goal = sVal[i].coordinate;
        }
    }
    throughBranches.emplace_back(goal);
    return true;
}