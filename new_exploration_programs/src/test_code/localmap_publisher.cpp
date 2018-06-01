#include <ros/ros.h>
#include <ros/callback_queue.h>
#include <sensor_msgs/PointCloud2.h>
#include <pcl_ros/point_cloud.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/filters/extract_indices.h>
#include <pcl/segmentation/sac_segmentation.h>
#include <pcl/segmentation/extract_clusters.h>
#include <limits>

#include <new_exploration_programs/segmented_cloud.h>
#include <new_exploration_programs/matching_info.h>

#include <std_msgs/Header.h>

#include <visualization_msgs/MarkerArray.h>

class LocalmapPublisher
{
private:
  ros::NodeHandle slm;
	ros::NodeHandle plm;

  ros::Subscriber slm_sub;
  ros::Publisher plm_pub;
  ros::Publisher plm_pub2;

  sensor_msgs::PointCloud2 localmap;
  sensor_msgs::PointCloud2 e_localmap;

  std_msgs::Header row_header;

public:

  bool input_lomap;
  int input_count;

  ros::CallbackQueue lm_queue;

  LocalmapPublisher()
  {
    slm.setCallbackQueue(&lm_queue);
    slm_sub = slm.subscribe("/centroid_matching/merged_cloud",1,&LocalmapPublisher::input_mergedcloud,this);
    plm_pub = plm.advertise<sensor_msgs::PointCloud2>("/localmap_publisher/merged_cloud", 1);
    plm_pub2 = plm.advertise<sensor_msgs::PointCloud2>("/localmap_publisher/localmap", 1);
    input_count = 0;
    input_lomap = false;
    e_localmap.header.frame_id = "camera_rgb_optical_frame";
    //e_localmap.height = 0;
  };
  ~LocalmapPublisher(){};

  void input_mergedcloud(const sensor_msgs::PointCloud2::ConstPtr& pc_msg);
  void publish_localmap(void);
  void publish_notlocalmap(void);
  void publish_emptylocalmap(void);
  void show_processtime(void);
};

void LocalmapPublisher::input_mergedcloud(const sensor_msgs::PointCloud2::ConstPtr& pc_msg)
{
  localmap = *pc_msg;
  std::cout << "input_localmap" << '\n';
  input_lomap = true;
  input_count++;
  row_header = pc_msg -> header;
  std::cout << "input << " << input_count << " << 回目" << '\n';
}

void LocalmapPublisher::publish_localmap(void)
{
  plm_pub2.publish(localmap);
  input_count = 0;

  std::cout << "publish_localmap" << '\n';
}

void LocalmapPublisher::publish_notlocalmap(void)
{
  plm_pub.publish(localmap);

  std::cout << "publish_not_localmap" << '\n';
}

void LocalmapPublisher::publish_emptylocalmap(void)
{
  //std::cout << "e_localmap" << e_localmap.header.frame_id << '\n';
  e_localmap.header = row_header;

  localmap = e_localmap;
  plm_pub.publish(localmap);


  //std::cout << "empty size" << localmap.width << '\n';


  std::cout << "publish_empty_localmap" << '\n';
}

void LocalmapPublisher::show_processtime(void)
{
  ros::Duration p_time = ros::Time::now() - row_header.stamp;
  double cycle_time = p_time.toSec();
  // std::cout << "rostimenow << " << ros::Time::now() << '\n';
  // std::cout << "rowheadertime << " << row_header.stamp << '\n';

  // std::cout << "emptycheck1 << " << localmap.header.seq << '\n';
  // std::cout << "emptycheck2 << " << localmap.header.stamp << '\n';
  // std::cout << "emptycheck3 << " << localmap.header.frame_id << '\n';

  std::cout << "this cycle time is << " << cycle_time << " [s]"  << '\n';
}

int main(int argc, char** argv)
{
	ros::init(argc, argv, "localmap_publisher");
	LocalmapPublisher lp;

  int localmap_th = 10;

	while(ros::ok()){
		lp.lm_queue.callOne(ros::WallDuration(1));
		if(lp.input_lomap)
		{
      if(lp.input_count == localmap_th)
      {
        lp.publish_localmap();
        lp.publish_emptylocalmap();
      }
      else
      {
        lp.publish_notlocalmap();
      }
      lp.show_processtime();
		}
		else
		{
			std::cout << '\n' << "not input" << '\n';
		}

    lp.input_lomap = false;
	}
	return 0;
}