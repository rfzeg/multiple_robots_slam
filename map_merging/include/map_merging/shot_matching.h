//shot特徴量のマッチングをテストする用のプログラムです
#include <ros/ros.h>
#include <ros/callback_queue.h>
#include <pcl_ros/point_cloud.h>
#include <pcl/filters/voxel_grid.h>

#include <pcl/correspondence.h>
#include <pcl/features/normal_3d_omp.h>
#include <pcl/features/shot_omp.h>
#include <pcl/features/board.h>
#include <pcl/recognition/cg/hough_3d.h>
#include <pcl/recognition/cg/geometric_consistency.h>
#include <pcl/kdtree/kdtree_flann.h>
#include <pcl/kdtree/impl/kdtree_flann.hpp>
#include <pcl/common/transforms.h>

#include <map_merging/Match.h>
#include <map_merging/PairNumber.h>


typedef pcl::PointXYZRGB PointType;
typedef pcl::Normal NormalType;
typedef pcl::ReferenceFrame RFType;
typedef pcl::SHOT352 DescriptorType;

class ShotMatching
{
private:
  ros::NodeHandle sS;
  ros::NodeHandle sM;

  ros::NodeHandle pS;

  ros::Subscriber subS;
  ros::Subscriber subM;

  ros::Publisher pubShotMatch;

  ros::NodeHandle pTest;
  ros::Publisher pubTest;

  map_merging::Match sMatch;

  map_merging::Cluster sCluster;
  map_merging::Cluster mCluster;

  Eigen::Matrix3f rotation;
  Eigen::Vector3f translation;

  //std_msgs::Int8 matchType;

  //bool input;
  bool inputS;
  bool inputM;
  bool matching;

  bool skip;


  //shot parameters
  bool use_hough_;
  float model_ss_;
  float scene_ss_;
  float rf_rad_;
  float descr_rad_;
  float cg_size_;
  float cg_thresh_;

  pcl::PointCloud<PointType>::Ptr model;
  pcl::PointCloud<PointType>::Ptr model_keypoints;
  pcl::PointCloud<PointType>::Ptr scene;
  pcl::PointCloud<PointType>::Ptr scene_keypoints;
  pcl::PointCloud<NormalType>::Ptr model_normals;
  pcl::PointCloud<NormalType>::Ptr scene_normals;
  pcl::PointCloud<DescriptorType>::Ptr model_descriptors;
  pcl::PointCloud<DescriptorType>::Ptr scene_descriptors;

  pcl::PointCloud<PointType>::Ptr sCloud;
  pcl::PointCloud<PointType>::Ptr mCloud;

  sensor_msgs::PointCloud2 testCloud;

public:
  ros::CallbackQueue queueS;
  ros::CallbackQueue queueM;

  ShotMatching();
	~ShotMatching(){};

  //void inputEigenMatch(const map_merging::Match::ConstPtr& sEMsg);
  void inputSource(const map_merging::Cluster::ConstPtr& sSMsg);
  void inputMerged(const map_merging::Cluster::ConstPtr& sMMsg);
  //bool isInput(void);
  bool isInputS(void);
  bool isInputM(void);
  void includeCloud(void);
  void resetFlag(void);
  //bool isMatch(void);
  void shotPublisher(void);
  void cluster2Scene(void);
  //void cluster2LinkCluster(void);
  //void cluster2AllCluster(void);
  void loadCluster(int clusterNum, bool isSource);
  void calcMatch(void);
  void setOutputData(std::vector<map_merging::PairNumber> &list, int clusterNum);
  bool isSkip(void);
  void setSkip(bool arg);
  void checkCloud(void);
};

ShotMatching::ShotMatching()
:model (new pcl::PointCloud<PointType>()),
model_keypoints (new pcl::PointCloud<PointType>()),
scene (new pcl::PointCloud<PointType>()),
scene_keypoints (new pcl::PointCloud<PointType>()),
model_normals (new pcl::PointCloud<NormalType>()),
scene_normals (new pcl::PointCloud<NormalType>()),
model_descriptors (new pcl::PointCloud<DescriptorType>()),
scene_descriptors (new pcl::PointCloud<DescriptorType>()),
sCloud (new pcl::PointCloud<PointType>()),
mCloud (new pcl::PointCloud<PointType>())
{
  sS.setCallbackQueue(&queueS);
  //subE = sE.subscribe("/map_merging/eigenValueMatching",1,&ShotMatching::inputEigenMatch,this);
  subS = sS.subscribe("/map_merging/clustering/sClustering",1,&ShotMatching::inputSource,this);

  sM.setCallbackQueue(&queueM);
  subM = sM.subscribe("/map_merging/clustering/mClustering",1,&ShotMatching::inputMerged,this);


  pubShotMatch = pS.advertise<map_merging::Match>("/map_merging/matching/shotMatching", 1);

  pubTest = pTest.advertise<sensor_msgs::PointCloud2>("/map_merging/test/testCloud", 1);

  //input = false;

  inputS = false;
  inputM = false;

  skip = false;

  matching = false;

  use_hough_ = true;

  model_ss_ = 0.1f;//キーポイント抽出の間隔(今回はダウンサンプリングするときのボクセルグリッドのサイズ)
  scene_ss_ = 0.1f;//キーポイント抽出の間隔(今回はダウンサンプリングするときのボクセルグリッドのサイズ)
  rf_rad_ = 0.4f;//重要っぽい//BOARD-LRFの半径//平面フィッティングするときに使う点群の半径//いじっても対応点の数には影響ない//でもマッチングしなくなる
  descr_rad_ = 0.4f;//各キーポイント周りのデスクプリタの抽出//キーポイントの周りのどれくらい情報を使うか//いじると対応点の数が変わる//マッチングにも影響あり
  cg_size_ = 0.3f;//ハフ空間に設定する各ビンのサイズ//輪郭を表す直線のサイズ//大きい方が判定が甘くなる?//ある程度大きい方がいい
  cg_thresh_ = 3.0f;//モデルインスタンスの存在をシーンクラウドに推論するために必要なHough空間内の最小票数を設定します。//要は一致判定のしきい値なので小さいほどゆるい

  // model_ss_ = 0.01f;
  // scene_ss_ = 0.03f;
  // rf_rad_ = 0.015f;
  // descr_rad_ = 0.02f;
  // cg_size_ = 0.01f;
  // cg_thresh_ = 5.0f;
}

void ShotMatching::inputSource(const map_merging::Cluster::ConstPtr& sSMsg)
{
  sCluster = *sSMsg;
  inputS = true;
  std::cout << "input source" << '\n';
}

void ShotMatching::inputMerged(const map_merging::Cluster::ConstPtr& sMMsg)
{
  mCluster = *sMMsg;
  inputM = true;
  std::cout << "input merged" << '\n';
}

void ShotMatching::includeCloud(void)
{
  pcl::fromROSMsg (sCluster.cloudObstacles, *sCloud);
  pcl::fromROSMsg (mCluster.cloudObstacles, *mCloud);
}

// void ShotMatching::inputEigenMatch(const map_merging::Match::ConstPtr& sEMsg)
// {
//   sMatch = *sEMsg;
//
//   input = true;
//
//   std::cout << "input EigenMatch" << '\n';
// }

// bool ShotMatching::isInput(void)
// {
//   return input;
// }

bool ShotMatching::isInputS(void)
{
  return inputS;
}

bool ShotMatching::isInputM(void)
{
  return inputM;
}

void ShotMatching::resetFlag(void)
{
  //input = false;
  inputS = false;
  inputM = false;
}

// bool ShotMatching::isMatch(void)
// {
//   return matching;
// }

void ShotMatching::setSkip(bool arg)
{
  skip = arg;
}

bool ShotMatching::isSkip(void)
{
  return skip;
}

void ShotMatching::shotPublisher(void)
{
  sMatch.matchType = 1;
  sMatch.sourceMap = sCluster;
  sMatch.mergedMap = mCluster;
  sMatch.header.stamp = ros::Time::now();

  pubShotMatch.publish(sMatch);
  std::cout << "published" << '\n';
}

void ShotMatching::cluster2Scene(void)
{
  /*リストにあるクラスタごとにシーン全体とSHOTマッチング*/
  /*sCloudとmCloudに入ってる*/
  *scene = *mCloud;//シーンがsCloudにある
  //*modelにマッチングするcloudを入れる(sCloudの中から)
  //クラスタの数//sCluster.clusterList.size()

  std::vector<map_merging::PairNumber> matchList;

  for(int i=0;i<sCluster.clusterList.size();i++)
  {
    loadCluster(i,true);//ここでi番目のクラスタが*modelに入る
    checkCloud();
    calcMatch();//ここでsceneとmodelのマッチングをする
    if(matching)
    {
      setOutputData(matchList,i);//マッチングの結果がrotationとtranslationに出てくるので
    }
  }

  sMatch.matchList = matchList;
  // for(int i=0;i<sMatch.matchList.size();i++)
  // {
  //   loadCluster(sMatch.matchList[i].sourceNumber,true);
  //   calcMatch();
  //   if(isMatch())
  //   {
  //     /*マッチリストの件*/
  //   }
  // }
  //
  // if(/*編集したマッチリストのサイズが1以上*/true)
  // {
  //   matching = true;
  // }

}

void ShotMatching::checkCloud(void)
{
  pcl::toROSMsg (*model, testCloud);

  testCloud.header.frame_id = "map";

  pubTest.publish(testCloud);
}

// void ShotMatching::cluster2LinkCluster(void)
// {
//   /*リストにあるクラスタごとに関連付けされたクラスタとSHOTマッチング*/
//   /*関連付けされてなかったら全部やる*/
//   pcl::fromROSMsg (sMatch.mergedMap.cloudObstacles, *mCloud);
//   pcl::fromROSMsg (sMatch.sourceMap.cloudObstacles, *sCloud);
//
//   std::vector<map_merging::PairNumber> matchList;
//
//   for(int i=0;i<sMatch.matchList.size();i++)
//   {
//     loadCluster(sMatch.matchList[i].sourceNumber,true);
//     loadCluster(sMatch.matchList[i].mergedNumber,false);
//     calcMatch();
//     if(isMatch())
//     {
//       matchList.push_back(sMatch.matchList[i]);
//     }
//   }
//
//   if(matchList.size()>0)
//   {
//     matching = true;
//   }
//
//   sMatch.matchList = matchList;
//
// }
//
// void ShotMatching::cluster2AllCluster(void)
// {
//   /*リストにあるクラスタごとにシーンにあるクラスタ全てとSHOTマッチング*/
//
// }

void ShotMatching::loadCluster(int clusterNum, bool isSource)
{
  //クラスタの番号とどっちのクラウドkaを受け取ってその番号のクラスタをpcl形式で作成する

  pcl::PointCloud<PointType>::Ptr cluster (new pcl::PointCloud<PointType>());
  /*読み込みの際に重心が原点になるようにする*/
  geometry_msgs::Point centroid;

  if(isSource)
  {
    //pcl::fromROSMsg (sMatch.sourceMap.cloudObstacles, *sCloud);
    for(int i=0;i<sCluster.clusterList[clusterNum].index.size();i++)
    {
      //sourceCloud->points[sMatch.sourceMap.clusterList[clusterNum].index[i]].x;
      //sourceCloud->points[sMatch.sourceMap.clusterList[clusterNum].index[i]].y;
      //sourceCloud->points[sMatch.sourceMap.clusterList[clusterNum].index[i]].z;

      cluster -> points.push_back(sCloud->points[sCluster.clusterList[clusterNum].index[i]]);
    }

    for(int i=0; i<cluster->points.size();i++)
    {
      cluster -> points[i].x -= sCluster.centroids[clusterNum].x;
      cluster -> points[i].y -= sCluster.centroids[clusterNum].y;
      cluster -> points[i].z -= sCluster.centroids[clusterNum].z;
    }

    /*cloudの設定をする*/
    cluster -> width = cluster -> points.size();
    cluster -> height = 1;
    cluster -> is_dense = false;



    *model = *cluster;

  }
  else
  {
    //pcl::fromROSMsg (sMatch.mergedMap.cloudObstacles, *mCloud);

    for(int i=0;i<mCluster.clusterList[clusterNum].index.size();i++)
    {
      //sourceCloud->points[sMatch.sourceMap.clusterList[clusterNum].index[i]].x;
      //sourceCloud->points[sMatch.sourceMap.clusterList[clusterNum].index[i]].y;
      //sourceCloud->points[sMatch.sourceMap.clusterList[clusterNum].index[i]].z;

      cluster -> points.push_back(mCloud->points[mCluster.clusterList[clusterNum].index[i]]);
    }

    for(int i=0; i<cluster->points.size();i++)
    {
      cluster -> points[i].x -= mCluster.centroids[clusterNum].x;
      cluster -> points[i].y -= mCluster.centroids[clusterNum].y;
      cluster -> points[i].z -= mCluster.centroids[clusterNum].z;
    }

    *scene = *cluster;
  }

}

void ShotMatching::setOutputData(std::vector<map_merging::PairNumber> &list ,int clusterNum)
{
  map_merging::PairNumber pair;

  geometry_msgs::Point centroid;

  centroid = sCluster.centroids[clusterNum];

  pair.sourceCentroid = centroid;

  centroid.x = translation(0);
  centroid.y = translation(1);
  centroid.z = translation(2);

  pair.mergedCentroid = centroid;

  std::vector<float> rot;
  for(int i=0;i<3;i++)
  {
    for(int j=0;j<3;j++)
    {
      rot.push_back(rotation(i,j));
    }
  }

  pair.rotation = rot;

  list.push_back(pair);

}


void ShotMatching::calcMatch(void)
{
  //
  //  Compute Normals
  //

  pcl::NormalEstimationOMP<PointType, NormalType> norm_est;
  norm_est.setKSearch (10);
  norm_est.setInputCloud (model);
  norm_est.compute (*model_normals);

  norm_est.setInputCloud (scene);
  norm_est.compute (*scene_normals);


  //
  // Compute keypoints
  //

  pcl::VoxelGrid<PointType> uniform_sampling;
  uniform_sampling.setInputCloud (model);
  uniform_sampling.setLeafSize (model_ss_,model_ss_,model_ss_);
  uniform_sampling.filter (*model_keypoints);
  std::cout << "Model total points: " << model->size () << "; Selected Keypoints: " << model_keypoints->size () << std::endl;

  uniform_sampling.setInputCloud (scene);
  uniform_sampling.setLeafSize (scene_ss_,scene_ss_,scene_ss_);
  uniform_sampling.filter (*scene_keypoints);
  std::cout << "Scene total points: " << scene->size () << "; Selected Keypoints: " << scene_keypoints->size () << std::endl;


  //
  //  Compute Descriptor for keypoints
  //

  pcl::SHOTEstimationOMP<PointType, NormalType, DescriptorType> descr_est;
  //pcl::SHOTColorEstimationOMP//色情報も使ってそう
  descr_est.setRadiusSearch (descr_rad_);
  descr_est.setInputCloud (model_keypoints);
  descr_est.setInputNormals (model_normals);
  descr_est.setSearchSurface (model);
  descr_est.compute (*model_descriptors);

  descr_est.setInputCloud (scene_keypoints);
  descr_est.setInputNormals (scene_normals);
  descr_est.setSearchSurface (scene);
  descr_est.compute (*scene_descriptors);

  pcl::CorrespondencesPtr model_scene_corrs (new pcl::Correspondences ());

  pcl::KdTreeFLANN<DescriptorType> match_search;
  match_search.setInputCloud (model_descriptors);

  //  For each scene keypoint descriptor, find nearest neighbor into the model keypoints descriptor cloud and add it to the correspondences vector.
  for (size_t i = 0; i < scene_descriptors->size (); ++i)
  {
    std::vector<int> neigh_indices (1);//要素数1で初期化
    std::vector<float> neigh_sqr_dists (1);//要素数1で初期化
    if (!pcl_isfinite (scene_descriptors->at (i).descriptor[0])) //skipping NaNs
    {
      continue;
    }
    int found_neighs = match_search.nearestKSearch (scene_descriptors->at (i), 1, neigh_indices, neigh_sqr_dists);
    if(found_neighs == 1 && neigh_sqr_dists[0] < 0.25f) //  add match only if the squared descriptor distance is less than 0.25 (SHOT descriptor distances are between 0 and 1 by design)
    //平方和記述子距離が0.25未満の場合にのみ一致を追加する（SHOT記述子距離は設計上0と1の間である）
    {
      pcl::Correspondence corr (neigh_indices[0], static_cast<int> (i), neigh_sqr_dists[0]);
      model_scene_corrs->push_back (corr);
    }
  }
  std::cout << "Correspondences found: " << model_scene_corrs->size () << std::endl;

  //
  //  Actual Clustering
  //
  std::vector<Eigen::Matrix4f, Eigen::aligned_allocator<Eigen::Matrix4f> > rototranslations;
  std::vector<pcl::Correspondences> clustered_corrs;

  //  Using Hough3D
  if (use_hough_)
  {
    //
    //  Compute (Keypoints) Reference Frames only for Hough
    //
    pcl::PointCloud<RFType>::Ptr model_rf (new pcl::PointCloud<RFType> ());
    pcl::PointCloud<RFType>::Ptr scene_rf (new pcl::PointCloud<RFType> ());

    pcl::BOARDLocalReferenceFrameEstimation<PointType, NormalType, RFType> rf_est;
    rf_est.setFindHoles (true);
    rf_est.setRadiusSearch (rf_rad_);

    rf_est.setInputCloud (model_keypoints);
    rf_est.setInputNormals (model_normals);
    rf_est.setSearchSurface (model);
    rf_est.compute (*model_rf);

    rf_est.setInputCloud (scene_keypoints);
    rf_est.setInputNormals (scene_normals);
    rf_est.setSearchSurface (scene);
    rf_est.compute (*scene_rf);

    //  Clustering
    pcl::Hough3DGrouping<PointType, PointType, RFType, RFType> clusterer;
    clusterer.setHoughBinSize (cg_size_);
    clusterer.setHoughThreshold (cg_thresh_);
    clusterer.setUseInterpolation (true);
    clusterer.setUseDistanceWeight (false);

    clusterer.setInputCloud (model_keypoints);
    clusterer.setInputRf (model_rf);
    clusterer.setSceneCloud (scene_keypoints);
    clusterer.setSceneRf (scene_rf);
    clusterer.setModelSceneCorrespondences (model_scene_corrs);

    //clusterer.cluster (clustered_corrs);
    clusterer.recognize (rototranslations, clustered_corrs);
  }
  else // Using GeometricConsistency
  {
    pcl::GeometricConsistencyGrouping<PointType, PointType> gc_clusterer;
    gc_clusterer.setGCSize (cg_size_);
    gc_clusterer.setGCThreshold (cg_thresh_);

    gc_clusterer.setInputCloud (model_keypoints);
    gc_clusterer.setSceneCloud (scene_keypoints);
    gc_clusterer.setModelSceneCorrespondences (model_scene_corrs);

    //gc_clusterer.cluster (clustered_corrs);
    gc_clusterer.recognize (rototranslations, clustered_corrs);
  }

  // if(rototranslations.size() > 0)
  // {
  //   matching = true;
  // }
  // else
  // {
  //   matching = false;
  // }



  //
  //  Output results
  //

  if(rototranslations.size () > 0)
  {
    std::cout << "****Matching!!****" << '\n';
    matching = true;
    rotation = rototranslations[0].block<3,3>(0, 0);
    translation = rototranslations[0].block<3,1>(0, 3);
  }
  else
  {
    std::cout << "****No Matching****" << '\n';
    matching = false;
  }
  // std::cout << "Model instances found: " << rototranslations.size () << std::endl;
  // for (size_t i = 0; i < rototranslations.size (); ++i)
  // {
  //   std::cout << "\n    Instance " << i + 1 << ":" << std::endl;
  //   std::cout << "        Correspondences belonging to this instance: " << clustered_corrs[i].size () << std::endl;
  //
  //   // Print the rotation matrix and translation vector
  //   Eigen::Matrix3f rotation = rototranslations[i].block<3,3>(0, 0);
  //   Eigen::Vector3f translation = rototranslations[i].block<3,1>(0, 3);
  //
  //   printf ("\n");
  //   printf ("            | %6.3f %6.3f %6.3f | \n", rotation (0,0), rotation (0,1), rotation (0,2));
  //   printf ("        R = | %6.3f %6.3f %6.3f | \n", rotation (1,0), rotation (1,1), rotation (1,2));
  //   printf ("            | %6.3f %6.3f %6.3f | \n", rotation (2,0), rotation (2,1), rotation (2,2));
  //   printf ("\n");
  //   printf ("        t = < %0.3f, %0.3f, %0.3f >\n", translation (0), translation (1), translation (2));
  // }

}
