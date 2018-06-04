#include <iostream>

#include <cxxopts.hpp>
#include <hiredis/hiredis.h>

#include <opencv2/opencv.hpp>

#include <rapidjson/rapidjson.h>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

#include <redisimagehelper/RedisImageHelper.hpp>

bool DEBUG = false;
std::string cameraKey = "custom:image";

static int parseCommandLine(cxxopts::Options options, int argc, char** argv)
{
    auto result = options.parse(argc, argv);
    if (result.count("help")) {
        std::cout << options.help({"", "Group"});
        return EXIT_FAILURE;
    }

    if (result.count("d")) {
        DEBUG = true;
        std::cerr << "Debug mode enabled." << std::endl;
    }

    if (result.count("k")) {
        cameraKey = result["k"].as<std::string>();
    }

    return 0;
}

int main(int argc, char** argv)
{
    cxxopts::Options options("artkmarkers", "Marker detection sample program using ARToolKitPlus library.");
    options.add_options()
            ("d, debug", "Enable debug mode. This will print helpfull process informations on the standard error stream.")
            ("k, key", "The redis key to fetch and put data on", cxxopts::value<std::string>())
            ("h, help", "Print help");

    int retCode = parseCommandLine(options, argc, argv);
    if (retCode) {
        std::cerr << retCode << std::endl;
        return EXIT_FAILURE;
    }

    RedisImageHelper client;
    if (!client.connect()) {
        std::cerr << "Cannot connect to redis server. Please ensure that a redis-server is up and running." << std::endl;
        return EXIT_FAILURE;
    }

    client.setCameraKey(cameraKey);

    size_t dataLength;
    char* markersData = client.getString(cameraKey + ":detected-markers", dataLength);
    Image* image = client.getImage();
    if (image == NULL)
    {
        std::cerr << "Could not fetch image data from key: " << cameraKey << std::endl;
        return EXIT_FAILURE;
    }
    uint width = image->width();
    uint height = image->height();
    cv::Mat matImg(height, width, CV_8UC3, image->data());
    cv::Mat toShow; cv::cvtColor(matImg, toShow, CV_RGB2BGR);

    if (DEBUG) { std::cerr << "Fetched markers data: " << std::endl << markersData << std::endl; }

    rapidjson::Document jsonMarkers;
    jsonMarkers.Parse(markersData);

    const rapidjson::Value& markers = jsonMarkers["markers"];
    if (!markers.IsArray())
    {
        std::cerr << "Failed to parse JSON." << std::endl;
        return EXIT_FAILURE;
    }

    for(rapidjson::SizeType i=0 ; i < markers.Size() ; i++) {
       const rapidjson::Value& marker = markers[i];
       int id = marker["id"].GetInt();
       int dir = marker["dir"].GetInt();
       std::string type = marker["type"].GetString();

       float center[2];
       const rapidjson::Value& centerData = marker["center"];
       if (!centerData.IsArray()) {
           std::cerr << "ERROR: Expected center data as an array" << std::endl;
       }
       center[0] = centerData[0].GetFloat();
       center[1] = centerData[1].GetFloat();

       cv::Scalar cornersColor(255, 0, 0), textColor(255, 255, 0);
       if (type.compare("CTag"))
       {
            cornersColor = cv::Scalar(0, 0, 255);
            textColor = cv::Scalar(255, 0, 255);
       }

       cv::putText(toShow, type + "#" + std::to_string(id), cv::Point(center[0], center[1]),
               cv::FONT_HERSHEY_COMPLEX_SMALL, 0.8, textColor, 1.1, CV_AA);

       const rapidjson::Value& cornersData = marker["corners"];
       if (!cornersData.IsArray()) {
           std::cerr << "ERROR: Expected corner data as an array." << std::endl;
       }
       if (cornersData.Size() != 8) {
           std::cerr << "ERROR: Corners array size must be 8." << std::endl;
       }

       float corners[8];
       for (rapidjson::SizeType i = 0 ; i < cornersData.Size() ; i++)
       {
           corners[i] = cornersData[i].GetFloat();
           if ((i+1)%2 == 0 && i != 0)
           {
               cv::circle(toShow, cv::Point(corners[i-1], corners[i]), 5, cornersColor, 3);
           }
       }
    }

    cv::imshow("frame", toShow);
    cv::waitKey(0);
}
