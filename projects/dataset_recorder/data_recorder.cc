// standard includes
#include<string>
#include<vector>
#include <thread>
#include <mutex>
#include <atomic>

// BoB includes
#include "common/main.h"
#include "common/stopwatch.h"
#include "common/background_exception_catcher.h"
#include "common/gps.h"
#include "common/map_coordinate.h"
#include "common/bn055_imu.h"

#include "imgproc/opencv_unwrap_360.h"
#include "video/panoramic.h"

#include <sys/stat.h>




// Standard C++ includes
#include <iostream>

using namespace BoBRobotics;
using namespace BoBRobotics::ImgProc;
using namespace BoBRobotics::Video;


#define WIDTH 192
#define HEIGHT 72

#define IS_UNWRAP true


void readCameraThreadFunc(BoBRobotics::Video::Input *cam,
                        cv::Mat &img,
                        std::atomic<bool> &shouldQuit,
                        std::mutex &mutex)
{

    //auto unwrapper = cam->createUnwrapper(cv::Size(WIDTH,HEIGHT));

    while(!shouldQuit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        std::lock_guard<std::mutex> lock(mutex);
        cam->readFrame(img);

    }


}

int bobMain(int, char **)
{

    using degree_t = units::angle::degree_t;
    std::vector<cv::Mat> images;
    std::vector<std::string> fileNames;

    // setting up gps
    const char *path_linux = "/dev/ttyACM1"; // path for linux systems
    BoBRobotics::GPS::Gps gps;
    gps.connect(path_linux);
    BoBRobotics::BN055 imu;
    const int maxTrials = 3;
    int numTrials = maxTrials;
    std::atomic<bool> shouldQuit{false};
    std::mutex mex;

    // check to see if we get valid coordinates by checking GPS quality
    while (numTrials > 0) {
        try {
            if (gps.getGPSData().gpsQuality != BoBRobotics::GPS::GPSQuality::INVALID) {
                std::cout << " we have a valid measurement" << std::endl;
                break;
            }
        } catch(...) {
            std::cout << " measuring failed, trying again in 1 second " << "[" << maxTrials - numTrials << "/" << maxTrials << "]" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            numTrials--;
        }
    }
    if (numTrials == 0) {
        std::cout << " There is no valid gps measurement, please try waiting for the survey in to finish and restart the program " << std::endl;
        exit(1);
    }

    std::string folderName;
    std::cin >> folderName;

    const char *c  = folderName.c_str();
    mkdir(c, 0777);
    std::cout << "directory " << c << " created" << std::endl;

    auto cam = getPanoramicCamera();
    const auto cameraRes = cam->getOutputSize();
    std::cout << "camera initialised" << std::endl;

    // unwrapper for camera
   // auto unwrapper = cam->createUnwrapper(cv::Size(WIDTH,HEIGHT));


    cv::Mat imgOrig;
    //--------------------------------->>>>>>>>>>> START RECORDING <<<<<<<<<<<<----------------------------


    // start camera thread
    cv::Mat originalImage, tmpImage, unwrappedImage;
    cv::Mat outputImage(cv::Size(WIDTH,HEIGHT), CV_8UC3);
    std::thread readCameraThread(&readCameraThreadFunc, cam.get(), std::ref(tmpImage), std::ref(shouldQuit), std::ref(mex));


    // create and open file to write gps coordinates
    std::ofstream coordinates;

    //write headers  for csv
    std::string csvpath = folderName + "/coordinates.csv";
    coordinates.open (csvpath, std::ofstream::trunc);
    coordinates  << "gps quality" << "," << "lat" << "," << "lon" << "," << "altitude"  << "," << "roll" << "," << "pitch" << "," << "yaw"  << ",";
    coordinates << "image name" << "," << "timestamp" <<"\n";
    coordinates.close();


    BoBRobotics::BackgroundExceptionCatcher catcher;
    catcher.trapSignals();

    int i = 0;
    BoBRobotics::Stopwatch sw, sw_timestamp;
    std::array<degree_t, 3> angles;
    // for x timestep
    sw_timestamp.start();
    for (;;) {

        try {
            catcher.check();
            sw.start();
            // get imu data
            angles = imu.getEulerAngles();
            float yaw = angles[0].value();
            float pitch = angles[1].value();
            float roll = angles[2].value();

            // get gps data
            BoBRobotics::GPS::GPSData data = gps.getGPSData();
            BoBRobotics::MapCoordinate::GPSCoordinate coord = data.coordinate;
            BoBRobotics::GPS::TimeStamp time = data.time;
            int gpsQual = (int) data.gpsQuality; // gps quality

            // output results
    	   // if (i%5 == 0) { // only output once ever sec
            std::cout << std::setprecision(10) << "GPS quality: " << gpsQual << " latitude: " << coord.lat.value()<< " longitude: " <<  coord.lon.value()  << " num sats: " << data.numberOfSatellites <<  std::endl;

	        std::ostringstream fileString, folderString;
            fileString << folderName << "/image" << i << ".png";
	        folderString << folderName << "/coordinates.csv";
            const std::string fileName = fileString.str();

            //  headers:   gpsQuality|latitude|longitude|altitude|roll|pitch|yaw|imagename|timestamp
            coordinates.open (folderString.str(), std::ofstream::app); // open coordinates.csv file and append
            coordinates << std::setprecision(10) << std::fixed << gpsQual << "," << coord.lat.value() << "," << coord.lon.value() << "," << data.altitude.value()  << "," << roll << "," << pitch << "," << yaw  << ",";
            coordinates << "image" << i << ".png" << "," << static_cast<units::time::millisecond_t>(sw_timestamp.elapsed()).value() <<"\n";
            coordinates.close();

            // poll from camera thread
            tmpImage.copyTo(originalImage);

            cv::Mat resized;
            if (originalImage.size().height > 0) {
                cv::resize(originalImage, resized, cv::Size(), 0.2, 0.2);
                cv::imwrite(fileName, originalImage);
                cv::imshow("orig",resized);
            }


            int count = (std::chrono::milliseconds(1000) - sw.elapsed()).count();
            //std::cout << " count " << count/1000000 << " " << std::endl;
            count /= 1000000;
            if ( cv::waitKey(count) == 27) {
                shouldQuit = true;
                readCameraThread.join();

                break;
            }
            i++;
        }
        catch(BoBRobotics::GPS::GPSError &e) {
            std::cout << e.what() << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(1000) - sw.elapsed());
        }
    }



    return 0;

}
