#include <ros/ros.h>
#include <ros/callback_queue.h>
#include <sensor_msgs/PointCloud2.h>
#include <pcl_ros/point_cloud.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/filters/extract_indices.h>
#include <pcl/segmentation/sac_segmentation.h>
#include <pcl/segmentation/extract_clusters.h>
#include <limits>


class ProcessingPointCloud
{
private:
	ros::NodeHandle ppcs;
	ros::NodeHandle ppcp;

	ros::Subscriber pc_sub;

	ros::Publisher pc_pub1;
	ros::Publisher pc_pub2;
	ros::Publisher pc_pub3;
	ros::Publisher pc_pub4;
	ros::Publisher pc_pub5;

	float camera_position_y;//カメラの高さ
	float ground_position_y;//どのくらいの高さまで床とするか

	float cloud_position;//表示の時それぞれの点群をどれくらい離すか

	float nan_c;

	/*ポイントクラウドの変数定義*/
	pcl::PointCloud<pcl::PointXYZ>::Ptr input_cloud;
	pcl::PointCloud<pcl::PointXYZ>::Ptr voxeled_cloud;
	pcl::VoxelGrid<pcl::PointXYZ> vg;

	pcl::PointCloud<pcl::PointXYZ>::Ptr deleted_ground_cloud;
	pcl::SACSegmentation<pcl::PointXYZ> seg;
	pcl::PointIndices::Ptr inliers;
	pcl::ModelCoefficients::Ptr coefficients;

	pcl::PointCloud<pcl::PointXYZ>::Ptr for_detect_ground_cloud;

	pcl::PointCloud<pcl::PointXYZRGB>::Ptr for_view_ground_cloud;
	pcl::ExtractIndices<pcl::PointXYZ> extract;

	//pcl::search::KdTree<pcl::PointXYZ>::Ptr tree;//何か探索用にツリーを作る

	//std::vector<pcl::PointIndices> cluster_indices;
	pcl::EuclideanClusterExtraction<pcl::PointXYZ> ec;

	pcl::PointCloud<pcl::PointXYZRGB>::Ptr clustered_cloud;


	sensor_msgs::PointCloud2 edit_cloud1;
	sensor_msgs::PointCloud2 edit_cloud2;
	sensor_msgs::PointCloud2 edit_cloud3;
	sensor_msgs::PointCloud2 edit_cloud4;

public:
	ros::CallbackQueue pc_queue;
	ProcessingPointCloud()
		:input_cloud(new pcl::PointCloud<pcl::PointXYZ>),
		voxeled_cloud(new pcl::PointCloud<pcl::PointXYZ>),
		deleted_ground_cloud (new pcl::PointCloud<pcl::PointXYZ>),
		inliers(new pcl::PointIndices),
		coefficients(new pcl::ModelCoefficients),
		for_detect_ground_cloud(new pcl::PointCloud<pcl::PointXYZ>),
		for_view_ground_cloud(new pcl::PointCloud<pcl::PointXYZRGB>),
		//tree (new pcl::search::KdTree<pcl::PointXYZ>),
		clustered_cloud(new pcl::PointCloud<pcl::PointXYZRGB>)
	{
		ppcs.setCallbackQueue(&pc_queue);
		pc_sub = ppcs.subscribe("/camera/depth_registered/points",1,&ProcessingPointCloud::input_pointcloud,this);
		pc_pub1 = ppcp.advertise<sensor_msgs::PointCloud2>("edit_cloud1", 1);
		pc_pub2 = ppcp.advertise<sensor_msgs::PointCloud2>("edit_cloud2", 1);
		pc_pub3 = ppcp.advertise<sensor_msgs::PointCloud2>("edit_cloud3", 1);
		pc_pub4 = ppcp.advertise<sensor_msgs::PointCloud2>("edit_cloud4", 1);
		//pc_pub5 = ppcp.advertise<sensor_msgs::PointCloud2>("edit_cloud5", 1);

		camera_position_y = 0.41;
		ground_position_y = 0.3;

		cloud_position = 5.0;

		nan_c = std::numeric_limits<float>::quiet_NaN();

		vg.setLeafSize (0.1f, 0.1f, 0.1f);

		seg.setOptimizeCoefficients (true);
		seg.setModelType (pcl::SACMODEL_PERPENDICULAR_PLANE);//ある軸に垂直な平面を抽出
		seg.setMethodType (pcl::SAC_RANSAC);
		seg.setMaxIterations (1000);//RANSACの繰り返し回数
		seg.setDistanceThreshold (0.1);//モデルとどのくらい離れていてもいいか???謎
		seg.setAxis(Eigen::Vector3f (0.0,1.0,0.0));//法線ベクトル
		seg.setEpsAngle(15.0f * (M_PI/180.0f));//許容出来る平面の傾きラジアン

		ec.setClusterTolerance (0.2);//同じクラスタとみなす距離
		ec.setMinClusterSize (100);//クラスタを構成する最小の点数
		ec.setMaxClusterSize (15000);//クラスタを構成する最大の点数
	};
	~ProcessingPointCloud(){};

	void input_pointcloud(const sensor_msgs::PointCloud2::ConstPtr& pc_msg);
	void apply_voxelgrid(void);
	void delete_ground(void);
	void euclidean_clustering(void);
	void publish_pointcloud(void);
};


void ProcessingPointCloud::input_pointcloud(const sensor_msgs::PointCloud2::ConstPtr& pc_msg)
{
	//pcl::PointCloud<pcl::PointXYZ>::Ptr input_cloud (new pcl::PointCloud<pcl::PointXYZ>);
	pcl::fromROSMsg (*pc_msg, *input_cloud);
	std::cout << "input_pointcloud" << std::endl;
}


void ProcessingPointCloud::apply_voxelgrid(void)
{
	//pcl::PointCloud<pcl::PointXYZ>::Ptr voxeled_cloud (new pcl::PointCloud<pcl::PointXYZ>);
	//pcl::VoxelGrid<pcl::PointXYZ> vg;
	vg.setInputCloud (input_cloud);
	vg.filter (*voxeled_cloud);
	std::cout << "pointcloud_voxeled" << std::endl;

/*表示用*/

	for(int i=0;i<voxeled_cloud->points.size();i++)
	{
		voxeled_cloud->points[i].x+=cloud_position;
	}

	pcl::toROSMsg (*voxeled_cloud, edit_cloud1);

}

void ProcessingPointCloud::delete_ground(void)
{
	/*ポイントクラウドを複製して高さが一定以上の点のみで平面を計算できるようにする(一定以上の高さだったらNanかInfにする)*/
	pcl::copyPointCloud(*voxeled_cloud, *for_detect_ground_cloud);

	for(int i=0;i<for_detect_ground_cloud->points.size();i++)
	{
		if(for_detect_ground_cloud->points[i].y < camera_position_y - ground_position_y)
		{
			for_detect_ground_cloud->points[i].x = nan_c;
			for_detect_ground_cloud->points[i].y = nan_c;
			for_detect_ground_cloud->points[i].z = nan_c;
		}
	}

	 seg.setInputCloud (for_detect_ground_cloud);
	 seg.segment (*inliers, *coefficients);

	 if (inliers->indices.size () == 0)
	 {
		 std::cout << "Could not estimate a planar model for the given dataset." << std::endl;
	 }

	 /*view_ground 推定した地面部分の色を赤で表示するだけ*/
	 //pcl::PointCloud<pcl::PointXYZRGB>::Ptr view_ground_cloud(new pcl::PointCloud<pcl::PointXYZRGB>);
	 pcl::copyPointCloud(*voxeled_cloud, *for_view_ground_cloud);

	 for (int i = 0; i < for_view_ground_cloud->points.size (); i++)
	 {
		 for_view_ground_cloud->points[i].r = 255;
		 for_view_ground_cloud->points[i].g = 255;
		 for_view_ground_cloud->points[i].b = 255;

		 if(for_view_ground_cloud->points[i].y < camera_position_y - ground_position_y)
		 {
			 for_view_ground_cloud->points[i].r = 0;
			 for_view_ground_cloud->points[i].g = 255;
			 for_view_ground_cloud->points[i].b = 0;
		 }
	 }

	 for (int i = 0; i < inliers->indices.size(); ++i)
	 {
		 for_view_ground_cloud->points[inliers->indices[i]].r = 255;
		 for_view_ground_cloud->points[inliers->indices[i]].g = 0;
		 for_view_ground_cloud->points[inliers->indices[i]].b = 0;
	 }

	 for(int i=0;i<for_view_ground_cloud->points.size();i++)
	 {
		 for_view_ground_cloud->points[i].x+=cloud_position;
	 }

	 pcl::toROSMsg (*for_view_ground_cloud, edit_cloud2);

	 //pcl::ExtractIndices<pcl::PointXYZ> extract;
	 extract.setInputCloud (voxeled_cloud);
	 extract.setIndices (inliers);

	 extract.setNegative (true);//true:平面を削除、false:平面以外削除
	 extract.filter (*deleted_ground_cloud);

	 std::cout << "ground_deleted" << std::endl;

	 for(int i=0;i<deleted_ground_cloud->points.size();i++)
	 {
		 deleted_ground_cloud->points[i].x+=2*cloud_position;
	 }

	 pcl::toROSMsg (*deleted_ground_cloud, edit_cloud3);

}




void ProcessingPointCloud::euclidean_clustering(void)
{
	pcl::search::KdTree<pcl::PointXYZ>::Ptr tree (new pcl::search::KdTree<pcl::PointXYZ>);//何か探索用にツリーを作る
	tree->setInputCloud (deleted_ground_cloud);
	std::vector<pcl::PointIndices> cluster_indices;

	ec.setSearchMethod (tree);
	ec.setInputCloud (deleted_ground_cloud);
	ec.extract (cluster_indices);

	int j = 0;
	float colors[12][3] ={{255,0,0},{0,255,0},{0,0,255},{255,255,0},{0,255,255},{255,0,255},{127,255,0},{0,127,255},{127,0,255},{255,127,0},{0,255,127},{255,0,127}};//色リスト
	pcl::copyPointCloud(*deleted_ground_cloud, *clustered_cloud);

	for (std::vector<pcl::PointIndices>::const_iterator it = cluster_indices.begin (); it != cluster_indices.end (); ++it)
  {
      for (std::vector<int>::const_iterator pit = it->indices.begin (); pit != it->indices.end (); pit++)
			{
					clustered_cloud->points[*pit].r = colors[j%12][0];
					clustered_cloud->points[*pit].g = colors[j%12][1];
					clustered_cloud->points[*pit].b = colors[j%12][2];
      }
      j++;
  }

	std::cout << "pointcloud_clustered" << std::endl;

	for(int i=0;i<clustered_cloud->points.size();i++)
	{
		clustered_cloud->points[i].x+=cloud_position;
	}

	pcl::toROSMsg (*clustered_cloud, edit_cloud4);
}

void ProcessingPointCloud::publish_pointcloud(void)
{
	pc_pub1.publish(edit_cloud1);
	pc_pub2.publish(edit_cloud2);
	pc_pub3.publish(edit_cloud3);
	pc_pub4.publish(edit_cloud4);
}

int main(int argc, char** argv)
{
	ros::init(argc, argv, "processing_pointcloud");
	ProcessingPointCloud pp;
	while(ros::ok()){
		//std::cout << "0" << std::endl;
		pp.pc_queue.callOne(ros::WallDuration(1));
		pp.apply_voxelgrid();
		pp.delete_ground();
		pp.euclidean_clustering();
		pp.publish_pointcloud();
	}
	return 0;
}
