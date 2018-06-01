#include <iostream>
#include <fstream>

#include <ARToolKitPlus/TrackerMultiMarker.h>
#include <cxxopts.hpp>
#include <hiredis/hiredis.h>

#include <opencv2/opencv.hpp>

#include <rapidjson/rapidjson.h>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

#include <redisimagehelper/RedisImageHelper.hpp>

std::string cameraKey = "custom:image";

bool DEBUG = false;

using ARToolKitPlus::TrackerMultiMarker;

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

static unsigned char* loadImage(std::string fileName, uint width, uint height)
{
    int numPixels = width * height, numBytesRead;
    unsigned char* data = new unsigned char[numPixels];
    if (FILE* fp = fopen(fileName.c_str(), "rb")) {
        numBytesRead = fread(data, 1, numPixels, fp);
        fclose(fp);
    } else {
        if (DEBUG) { std::cerr << "Failed to open " << fileName << std::endl; }
        return NULL;
    }

    if (numBytesRead != numPixels) {
        if(DEBUG) { std::cerr << "Failed to read " << fileName << std::endl; }
        return NULL;
    }
    return data;
}

int main(int argc, char** argv)
{
    cxxopts::Options options("artkmarkers", "Marker detection sample program using ARToolKitPlus library.");
    options.add_options()
            ("d, debug", "Enable debug mode. This will print helpfull process informations on the standard error stream.")
            ("c, camera-calibration", "The camera calibration file that will be used to correct distortions.", cxxopts::value<std::string>())
            ("k, key", "The redis key to fetch and put data on", cxxopts::value<std::string>())
            ("h, help", "Print help");

    auto result = options.parse(argc, argv);
    if (result.count("help")) {
        std::cout << options.help({"", "Group"});
        return EXIT_FAILURE;
    }

    if (result.count("d")) { DEBUG = true; std::cerr << "Debug mode enabled." << std::endl; }

    std::string cameraFile, imageFile;
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
    uint width = image->width();
    uint height = image->height();

    unsigned char* data = rgb_to_gray(width, height, image->data());

    TrackerMultiMarker tracker(width, height, 20, 6, 6, 6, 20);
    tracker.setPixelFormat(ARToolKitPlus::PIXEL_FORMAT_LUM);
    bool init = tracker.init(cameraFile.c_str(), "../data/markerboard_480-499.cfg", 1.0f, 1000.0f);
    if (!init)
    {
        if (DEBUG) { std::cerr << "Could not initialize Tracker" << std::endl; }
        return EXIT_FAILURE;
    }

    if (DEBUG) { tracker.getCamera()->printSettings(); }
    tracker.activateAutoThreshold(true);
    tracker.setMarkerMode(ARToolKitPlus::MARKER_ID_BCH);
    tracker.setBorderWidth(0.125); // BCH markers
    tracker.setUndistortionMode(ARToolKitPlus::UNDIST_NONE);
    //tracker.setImageProcessingMode(ARToolKitPlus::IMAGE_FULL_RES);
    //tracker.setUseDetectLite(true);

    tracker.calc(data);
    int markersCount = tracker.getNumDetectedMarkers();

    if (DEBUG) {
        std::cerr << "Found " << markersCount << std::endl;
    }

    cv::Mat mat(height, width, CV_8UC1, data), toShow;
    cv::cvtColor(mat, toShow, CV_GRAY2BGR);

    // Building JSON from markers information
    // Creating JSON data structure that will hold markers information
    rapidjson::Document jsonMarkers;
    jsonMarkers.SetObject();
    rapidjson::Document::AllocatorType& allocator = jsonMarkers.GetAllocator();

    // markersObj will be the array holding each individual marker objects.
    rapidjson::Value markersObj(rapidjson::kArrayType);

    int* markersId = new int[markersCount];
    tracker.getDetectedMarkers(markersId);
    for(int i = 0 ; i < markersCount ; i++)
    {
        // markerObj is a single marker object holding markers data
        rapidjson::Value markerObj(rapidjson::kObjectType);

        auto markerInfo = tracker.getDetectedMarker(i);
        if (DEBUG) {
            std::cerr << "Markers #" << markersId[i] << std::endl
                      << "[Info]" << std::endl
                      << "\t" << "pos: " << markerInfo.pos[0] << ";" << markerInfo.pos[1] << std::endl
                    << "\t" << "dir: " << markerInfo.dir << std::endl
                    << "\t" << "confidence: " << int(markerInfo.cf * 100.0) << "%" << std::endl;
        }

        markerObj.AddMember("id", markersId[i], allocator);
        markerObj.AddMember("dir", markerInfo.dir, allocator);
        markerObj.AddMember("confidence", int(markerInfo.cf * 100.0), allocator);

        rapidjson::Value array (rapidjson::kArrayType);
        for (int points = 0 ; points < 4; points++)
        {
            array.PushBack(markerInfo.vertex[points][0], allocator);
            array.PushBack(markerInfo.vertex[points][0], allocator);

            cv::Point p(markerInfo.vertex[points][0], markerInfo.vertex[points][1]);
            cv::circle(toShow, cv::Point(p), 5, cv::Scalar(0, 0, 255) , 3);
        }

        // Filling the marker obj with the corners data
        markerObj.AddMember("corners", array, allocator);
        // Filling the markers with the generated marker object
        markersObj.PushBack(markerObj, allocator);

        cv::putText(toShow, std::to_string(markersId[i]), cv::Point(markerInfo.pos[0], markerInfo.pos[1]),
                cv::FONT_HERSHEY_COMPLEX_SMALL, 0.8, cv::Scalar(0, 255, 0), 1, CV_AA);
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

    cv::imshow("gray frame", toShow);
    cv::waitKey(0);

    return EXIT_SUCCESS;
}
