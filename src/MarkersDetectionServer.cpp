#include <iostream>
#include <fstream>

// Markers detection libraries
#include <ARToolKitPlus/TrackerMultiMarker.h>
#include <chilitags/chilitags.hpp>

// Command line options library
#include <cxxopts.hpp>

// Redis C bindings
#include <hiredis/hiredis.h>
#include <hiredis/async.h>
#include <hiredis/adapters/libevent.h>

// JSON formatting library
#include <rapidjson/rapidjson.h>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

// Custom redis image helper library
#include <redisimagehelper/RedisImageHelper.hpp>

// Opencv (required by chilitags)
#include <opencv2/opencv.hpp>

bool VERBOSE = false;
bool STREAM_MODE = true;
std::string redisInputKey = "custom:image";
std::string redisOutputKey = "custom:image:output";
std::string redisHost = "127.0.0.1";
int redisPort = 6379;
std::string cameraCalibrationFile = "../data/no_distortion.cal";

using ARToolKitPlus::TrackerMultiMarker;
using chilitags::Chilitags;

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

    if (result.count("c")) {
        cameraCalibrationFile = result["c"].as<std::string>();
        if (VERBOSE) {
            std::cerr << "Camera file specified. Using " << cameraCalibrationFile << " as camera calibration file." << std::endl;
        }
    }
    else {
        if (VERBOSE) {
            std::cerr << "No camera configuration file specified. Using default camera configuration file: " << cameraCalibrationFile << std::endl;
        }
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

    if (result.count("o")) {
        redisOutputKey = result["o"].as<std::string>();
        if (VERBOSE) {
            std::cerr << "Output key was set to `" << redisOutputKey << "`." << std::endl;
        }
    }
    else {
        if (VERBOSE) {
            std::cerr << "No output key was specified. Output key was set to default (" << redisOutputKey << ")." << std::endl;
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

static unsigned char* rgb_to_gray(uint width, uint height, unsigned char* rgb)
{
    unsigned char* gray = new unsigned char[width * height];
    int cpt = 0;
    for (int i = 0 ; i < width * height * 3 ; i+=3)
    {
        gray[cpt++] = rgb[i];
    }
    return gray;
}

static TrackerMultiMarker* detectARTKMarkers(unsigned char* grayImage, uint width, uint height)
{
    TrackerMultiMarker* tracker = new TrackerMultiMarker(width, height, 20, 6, 6, 6, 20);
    tracker->setPixelFormat(ARToolKitPlus::PIXEL_FORMAT_LUM);
    bool init = tracker->init(cameraCalibrationFile.c_str(), "../data/markerboard_480-499.cfg", 1.0f, 1000.0f);
    if (!init)
    {
        if (VERBOSE) { std::cerr << "Could not initialize Tracker" << std::endl; }
        return NULL;
    }

    if (VERBOSE) { tracker->getCamera()->printSettings(); }

    /* Marker detection options */
    tracker->activateAutoThreshold(true);
    tracker->setMarkerMode(ARToolKitPlus::MARKER_ID_BCH);
    tracker->setBorderWidth(0.125); // BCH markers
    tracker->setUndistortionMode(ARToolKitPlus::UNDIST_NONE);
    tracker->setImageProcessingMode(ARToolKitPlus::IMAGE_FULL_RES);
    //    tracker->setPoseEstimator(ARToolKitPlus::POSE_ESTIMATOR_RPP);
    tracker->setUseDetectLite(true);

    tracker->calc(grayImage);
    return tracker;
}

static rapidjson::Value* ARTKMarkerToJSON(const ARToolKitPlus::ARMarkerInfo& markerInfo, rapidjson::Document::AllocatorType& allocator)
{
    rapidjson::Value* markerObj = new rapidjson::Value(rapidjson::kObjectType);
    if (VERBOSE) {
        std::cerr << "Markers #" << markerInfo.id << std::endl
                  << "[Info]" << std::endl
                  << "\t" << "pos: " << markerInfo.pos[0] << ";" << markerInfo.pos[1] << std::endl
                  << "\t" << "dir: " << markerInfo.dir << std::endl
                  << "\t" << "confidence: " << int(markerInfo.cf * 100.0) << "%" << std::endl;
    }

    markerObj->AddMember("id", markerInfo.id, allocator);
    markerObj->AddMember("dir", markerInfo.dir, allocator);
    markerObj->AddMember("confidence", int(markerInfo.cf * 100.0), allocator);
    markerObj->AddMember("type", "ARTK", allocator);

    rapidjson::Value centerArray (rapidjson::kArrayType);
    centerArray.PushBack(markerInfo.pos[0], allocator);
    centerArray.PushBack(markerInfo.pos[1], allocator);
    markerObj->AddMember("center", centerArray, allocator);

    rapidjson::Value cornerArray (rapidjson::kArrayType);
    // WARNING: corners should be pushed in the following order:
    // top left - top right - bot right - bot left
    cornerArray.PushBack(markerInfo.vertex[2][0], allocator);
    cornerArray.PushBack(markerInfo.vertex[2][1], allocator);

    cornerArray.PushBack(markerInfo.vertex[3][0], allocator);
    cornerArray.PushBack(markerInfo.vertex[3][1], allocator);

    cornerArray.PushBack(markerInfo.vertex[0][0], allocator);
    cornerArray.PushBack(markerInfo.vertex[0][1], allocator);

    cornerArray.PushBack(markerInfo.vertex[1][0], allocator);
    cornerArray.PushBack(markerInfo.vertex[1][1], allocator);
    
    // Filling the marker obj with the corners data
    markerObj->AddMember("corners", cornerArray, allocator);
    return markerObj;
}

static chilitags::TagCornerMap* detectChilitagsMarkers(unsigned char* grayImage, uint width, uint height)
{
    // Create opencv data structure since chilitags works only with opencv matrices
    cv::Mat image(height, width, CV_8UC1, grayImage);

    // Detect chilitags markers by calling 'find'
    Chilitags chilitags;
    chilitags::TagCornerMap* tags = new chilitags::TagCornerMap();
    *tags = chilitags.find(image);
    return tags;
}

static rapidjson::Value* CTagToJSON(const std::pair<int, chilitags::Quad>& tag, rapidjson::Document::AllocatorType& allocator)
{
    rapidjson::Value* tagObj = new rapidjson::Value(rapidjson::kObjectType);
    int id = tag.first;
    //TODO: Compute it.
    int dir = 0;
    tagObj->AddMember("id", id, allocator);
    tagObj->AddMember("dir", dir, allocator);
    tagObj->AddMember("confidence", 100, allocator);
    tagObj->AddMember("type", "CTag", allocator);

    const cv::Mat_<cv::Point2f> corners(tag.second);

    cv::Point2f center = 0.5f * (corners(0) + corners(2));

    rapidjson::Value centerArray(rapidjson::kArrayType);
    centerArray.PushBack(center.x, allocator);
    centerArray.PushBack(center.y, allocator);
    tagObj->AddMember("center", centerArray, allocator);

    rapidjson::Value cornerArray(rapidjson::kArrayType);
    for (int points = 0 ; points < 4 ; ++points)
    {
        cornerArray.PushBack(corners(points).x, allocator);
        cornerArray.PushBack(corners(points).y, allocator);
    }

    tagObj->AddMember("corners", cornerArray, allocator);
    return tagObj;
}

void onImageDataRecieved(redisAsyncContext* c, void* rep, void* privdata) {
    redisReply *reply = (redisReply*)rep;
    if (reply == NULL){
        cout <<"Response not recevied" << endl;
        return;
    }

    if(reply->type == REDIS_REPLY_ARRAY & reply->elements == 3)
    {
        if(strcmp( reply->element[0]->str, "subscribe") != 0)
        {
            std::cerr << "Message recieved on channel `" << reply->element[1]->str << "`" << std::endl;
        }
    }
}

int main(int argc, char** argv)
{
    cxxopts::Options options("markers-detection-server", "Marker detection sample program using ARToolKitPlus library & redis.");
    options.add_options()
            ("i, input", "The redis input key where data are going to arrive.", cxxopts::value<std::string>())
            ("o, output", "The redis output key where to set output data.", cxxopts::value<std::string>())
            ("s, stream", "Activate stream mode. In stream mode the program will constantly process input data and publish output data. By default stream mode is enabled.")
            ("u, unique", "Activate unique mode. In unique mode the program will only read and output data one time.")
            ("redis-port", "The port to which the redis client should try to connect.", cxxopts::value<int>())
            ("redis-host", "The host adress to which the redis client should try to connect", cxxopts::value<std::string>())
            ("c, camera-calibration", "The camera calibration file that will be used to adjust the results depending on the physical camera characteristics.", cxxopts::value<std::string>())
            ("v, verbose", "Enable verbose mode. This will print helpfull process informations on the standard error stream.")
            ("h, help", "Print this help message.");

    int retCode = parseCommandLine(options, argc, argv);
    if (retCode)
    {
        return EXIT_FAILURE;
    }

    RedisImageHelper client(redisHost, redisPort, redisInputKey);
    if (!client.connect()) {
        std::cerr << "Cannot connect to redis server. Please ensure that a redis-server is up and running." << std::endl;
        return EXIT_FAILURE;
    }

    // If stream mode we need a client to publish & another redis asynchronous context to subscribe
    struct event_base* event = event_base_new();
    redisAsyncContext* asyncRedis = redisAsyncConnect(redisHost, redisPort);
    redisLibeventAttach(asyncRedis, event);

    if (STREAM_MODE) {
        redisAsyncCommand(asyncRedis, onImageDataRecieved, (char*)"", "SUBSCRIBE %s", redisInputKey.c_str());
    }
    else {
        Image* image = client.getImage(redisInputKey);
        if (image == NULL) {
            if (VERBOSE) {
                std::cerr << "Could not fetch image data from redis server. Please ensure that the key you provided data from is correct." << std::endl;
            }
            return EXIT_FAILURE;
        }

        // Getting image info from Redis
        uint width = image->width();
        uint height = image->height();
        unsigned char* data = rgb_to_gray(width, height, image->data());

        // Creating JSON data structure that will hold markers information
        rapidjson::Document jsonMarkers;
        jsonMarkers.SetObject();
        rapidjson::Document::AllocatorType& allocator = jsonMarkers.GetAllocator();
        // markersObj will be the array holding each individual marker objects.
        rapidjson::Value markersObj(rapidjson::kArrayType);

        // Detect ARToolkitMarkers
        TrackerMultiMarker* ARTKTracker = detectARTKMarkers(data, width, height);
        if (ARTKTracker == NULL)
        {
            return EXIT_FAILURE;
        }
        int markersCount = ARTKTracker->getNumDetectedMarkers();
        if (VERBOSE) {
            std::cerr << "Found " << markersCount <<  " ARToolKitPlus markers." << std::endl;
        }

        for(int i = 0 ; i < markersCount ; i++)
        {
            auto markerInfo = ARTKTracker->getDetectedMarker(i);
            // Converting markerInfo to rapidjson obj
            rapidjson::Value* markerObj = ARTKMarkerToJSON(markerInfo, allocator);

            // Filling the markers with the generated marker object
            markersObj.PushBack(*markerObj, allocator);
            delete markerObj;
        }

        // Detect Chilitags markers
        chilitags::TagCornerMap* chilitagsMarkers = detectChilitagsMarkers(data, width, height);
        if (chilitagsMarkers == NULL) {
            return EXIT_FAILURE;
        }
        markersCount = chilitagsMarkers->size();
        if (VERBOSE) {
            std::cerr << "Found " << markersCount << " Chilitags markers." << std::endl;
        }

        for (const std::pair<int, chilitags::Quad>& tag : *chilitagsMarkers)
        {
            rapidjson::Value* tagObj = CTagToJSON(tag, allocator);
            markersObj.PushBack(*tagObj, allocator);
            delete tagObj;
        }

        // Finally putting everything on the document object
        jsonMarkers.AddMember("markers", markersObj, allocator);

        rapidjson::StringBuffer strbuf;
        rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
        jsonMarkers.Accept(writer);

        client.setString((char*)strbuf.GetString(), redisOutputKey);
        client.publishString((char*)strbuf.GetString(), redisOutputKey);
        if (VERBOSE) {
            std::cerr << strbuf.GetString() << std::endl;
        }

        //if (ARTKTracker != NULL) { delete ARTKTracker; }
        //if (chilitagsMarkers != NULL) { delete chilitagsMarkers; }
    }

    return EXIT_SUCCESS;
}
