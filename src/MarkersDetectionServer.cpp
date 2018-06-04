#include <iostream>
#include <fstream>

// Markers detection libraries
#include <ARToolKitPlus/TrackerMultiMarker.h>
#include <chilitags/chilitags.hpp>

// Command line options library
#include <cxxopts.hpp>

// Redis C bindings
#include <hiredis/hiredis.h>

// JSON formatting library
#include <rapidjson/rapidjson.h>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

// Custom redis helper library
#include <redisimagehelper/RedisImageHelper.hpp>

// Opencv (required by chilitags)
#include <opencv2/opencv.hpp>

bool DEBUG = false;
std::string cameraKey = "custom:image";
std::string cameraFile = "../data./no_distortion.cal";

using ARToolKitPlus::TrackerMultiMarker;
using chilitags::Chilitags;

static int parseCommandLine(cxxopts::Options options, int argc, char** argv)
{
    auto result = options.parse(argc, argv);
    if (result.count("help")) {
        std::cout << options.help({"", "Group"});
        return EXIT_FAILURE;
    }

    if (result.count("d")) { DEBUG = true; std::cerr << "Debug mode enabled." << std::endl; }

    if (result.count("c")) {
        cameraFile = result["c"].as<std::string>();
    }
    else {
        cameraFile = "../data/no_distortion.cal";
        if (DEBUG) { std::cerr << "No camera configuration file specified. Using default camera configuration file: " << cameraFile << std::endl; }
    }

    if (result.count("k")) {
        cameraKey = result["k"].as<std::string>();
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
    bool init = tracker->init(cameraFile.c_str(), "../data/markerboard_480-499.cfg", 1.0f, 1000.0f);
    if (!init)
    {
        if (DEBUG) { std::cerr << "Could not initialize Tracker" << std::endl; }
        return NULL;
    }

    if (DEBUG) { tracker->getCamera()->printSettings(); }

    /* Marker detection options */
    tracker->activateAutoThreshold(true);
    tracker->setMarkerMode(ARToolKitPlus::MARKER_ID_BCH);
    tracker->setBorderWidth(0.125); // BCH markers
    tracker->setUndistortionMode(ARToolKitPlus::UNDIST_NONE);
    tracker->setImageProcessingMode(ARToolKitPlus::IMAGE_FULL_RES);
    tracker->setUseDetectLite(true);

    tracker->calc(grayImage);
    return tracker;
}

static rapidjson::Value* ARTKMarkerToJSON(const ARToolKitPlus::ARMarkerInfo& markerInfo, rapidjson::Document::AllocatorType& allocator)
{
    rapidjson::Value* markerObj = new rapidjson::Value(rapidjson::kObjectType);
    if (DEBUG) {
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
    for (int points = 0 ; points < 4; ++points)
    {
        cornerArray.PushBack(markerInfo.vertex[points][0], allocator);
        cornerArray.PushBack(markerInfo.vertex[points][1], allocator);
    }

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
    int dir = 0; //TODO: Compute it (easy)
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

int main(int argc, char** argv)
{
    cxxopts::Options options("artkmarkers", "Marker detection sample program using ARToolKitPlus library.");
    options.add_options()
            ("d, debug", "Enable debug mode. This will print helpfull process informations on the standard error stream.")
            ("c, camera-calibration", "The camera calibration file that will be used to correct distortions.", cxxopts::value<std::string>())
            ("k, key", "The redis key to fetch and put data on", cxxopts::value<std::string>())
            ("h, help", "Print help");

    int retCode = parseCommandLine(options, argc, argv);
    if (retCode)
    {
        return EXIT_FAILURE;
    }

    RedisImageHelper client;
    if (!client.connect()) {
        if (DEBUG) {
            std::cerr << "Cannot connect to redis server. Please ensure that a redis-server is up and running." << std::endl;
        }
        return EXIT_FAILURE;
    }

    client.setCameraKey(cameraKey);
    Image* image = client.getImage();
    if (image == NULL) {
        if (DEBUG) {
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
    if (DEBUG) {
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
    if (DEBUG) {
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

    client.setString(cameraKey + ":detected-markers", (char*)strbuf.GetString());
    if (DEBUG) {
        std::cerr << strbuf.GetString() << std::endl;
    }

    delete ARTKTracker, chilitagsMarkers;
    return EXIT_SUCCESS;
}
