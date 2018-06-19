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
std::string mainKey = "custom:image";

struct contextData {
    uint width;
    uint height;
    uint channels;
};

static int parseCommandLine(cxxopts::Options options, int argc, char** argv)
{
    auto result = options.parse(argc, argv);
    if (result.count("h")) {
        std::cout << options.help({"", "Group"});
        return EXIT_FAILURE;
    }

    if (result.count("v")) {
        VERBOSE = true;
        std::cerr << "Verbose mode enabled." << std::endl;
    }

    if (result.count("i")) {
        redisInputKey = result["i"].as<std::string>();
        if (VERBOSE) {
            std::cerr << "Input key was set to `" << redisInputKey << "`." << std::endl;
        }
    }
    else {
        if (VERBOSE) {
            std::cerr << "No input key was specified. Input key was set to default (" << redisInputKey << ")." << std::endl;
        }
    }

    if (result.count("u")) {
        STREAM_MODE = false;
        if (VERBOSE) {
            std::cerr << "Unique mode enabled." << std::endl;
        }
    }

    if (result.count("redis-port")) {
        redisPort = result["redis-port"].as<int>();
        if (VERBOSE) {
            std::cerr << "Redis port set to " << redisPort << "." << std::endl;
        }
    }
    else {
        if (VERBOSE) {
            std::cerr << "No redis port specified. Redis port was set to " << redisPort << "." << std::endl;
        }
    }

    if (result.count("redis-host")) {
        redisHost = result["redis-host"].as<std::string>();
        if (VERBOSE) {
            std::cerr << "Redis host set to " << redisHost << "." << std::endl;
        }
    }
    else {
        if (VERBOSE) {
            std::cerr << "No redis host was specified. Redis host was set to " << redisHost << "." << std::endl;
        }
    }
    return 0;
}

void parseMarkerJson() {

}

void onMarkersDataPublished (redisAsyncContext* context, void* rep, void* privdata) {
    redisReply *reply = (redisReply*) rep;
    if  (reply == NULL) { return; }
    if (reply->type != REDIS_REPLY_ARRAY || reply->elements != 3) {
        if (VERBOSE) {
            std::cerr << "Error: Bad reply format." << std::endl;
        }
        return;
    }

    struct contextData* data = static_cast<struct contextData*>(privdata);
    if (data == NULL) {
        if(VERBOSE) {
            std::cerr << "Could not data from private data." << std::endl;
        }
        return;
    }
    uint width = data->width;
    uint height = data->height;
    uint channels = data->channels;


}

int main(int argc, char** argv)
{
    cxxopts::Options options("markers-detection-client", "Marker detection sample program using ARToolKitPlus library & redis.");
    options.add_options()
            ("redis-port", "The port to which the redis client should try to connect.", cxxopts::value<int>())
            ("redis-host", "The host adress to which the redis client should try to connect", cxxopts::value<std::string>())
            ("i, input", "The redis input key where data are going to arrive.", cxxopts::value<std::string>())
            ("s, stream", "Activate stream mode. In stream mode the program will constantly process input data and publish output data. By default stream mode is enabled.")
            ("u, unique", "Activate unique mode. In unique mode the program will only read and output data one time.")
            ("v, verbose", "Enable verbose mode. This will print helpfull process informations on the standard error stream.")
            ("h, help", "Print this help message.");

    int retCode = parseCommandLine(options, argc, argv);
    if (retCode) {
        return EXIT_FAILURE;
    }

    RedisImageHelperSync clientSync(redisHost, redisPort, redisInputKey);
    if (!clientSync.connect()) {
        std::cerr << "Cannot connect to redis server. Please ensure that a redis server is up and running." << std::endl;
        return EXIT_FAILURE;
    }

    struct contextData data;
    data.width = clientSync.getInt(redisInputKey + ":width");
    data.height = clientSync.getInt(redisInputKey + ":height");
    data.channels = clientSync.getInt(redisInputKey + ":channels");

    if (STREAM_MODE) {
        // In stream mode we need another client that will subscribe to the input key channel.
        RedisImageHelperAsync clientAsync(redisHost, redisPort, redisInputKey);
        if (!clientAsync.connect()) {
            std::cerr << "Cannot connect to redis server. Please ensure that a redis server is up and running." << std::endl;
            return EXIT_FAILURE;
        }
        clientAsync.subscribe(redisInputKey, onMarkersDataPublished, static_cast<void*>(&data));
    }
    else {

    }

            /*
    size_t dataLength;
    char* markersData = client.getString(mainKey + ":detected-markers", dataLength);
    Image* image = client.getImage();
    if (image == NULL)
    {
        std::cerr << "Could not fetch image data from key: " << mainKey << std::endl;
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
    */
}
