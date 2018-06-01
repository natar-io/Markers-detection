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

int main(int argc, char** argv)
{
    RedisImageHelper client;
    if (!client.connect()) {
        if (DEBUG) {
            std::cerr << "Cannot connect to redis server. Please ensure that a redis-server is up and running." << std::endl;
        }
        return EXIT_FAILURE;
    }

    client.setCameraKey(cameraKey);

    size_t dataLength;
    char* markersData = client.getString(cameraKey + ":detected-markers", dataLength);
    Image* image = client.getImage();

    if (DEBUG) { std::cerr << "Fetched markers data: " << std::endl << markersData << std::endl; }

    rapidjson::Document jsonMarkers;
    jsonMarkers.Parse(markersData);

    const rapidjson::Value& markers = jsonMarkers["markers"];
    if (!markers.IsArray())
    {
        std::cerr << "Failed to parse JSON." << std::endl;
        return EXIT_FAILURE;
    }

    for(rapidjson::SizeType i ; i < markers.Size() ; i++)
    {
       const rapidjson::Value& marker = markers[i];
       //TODO: Fetch marker datas and print image with detected corners
    }
}
