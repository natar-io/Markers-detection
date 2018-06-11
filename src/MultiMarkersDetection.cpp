#include <iostream>
#include <fstream>

#include <ARToolKitPlus/TrackerMultiMarker.h>
#include <cxxopts.hpp>

bool DEBUG = false;
std::string cameraFile = "../data/no_distortion.cal";
std::string imageFile = "../data/markerboard_480-499.raw";

using ARToolKitPlus::TrackerMultiMarker;

static int parseCommandLine(cxxopts::Options options, int argc, char** argv)
{
    auto result = options.parse(argc, argv);
    if (result.count("h")) {
        std::cout << options.help({"", "Group"});
        return EXIT_FAILURE;
    }

    if (result.count("d")) { 
        DEBUG = true; 
        std::cerr << "Debug mode enabled." << std::endl;
    }

    if (result.count("c")) {
        cameraFile = result["c"].as<std::string>();
    }
    else {
        if (DEBUG) { std::cerr << "No camera configuration file specified. Using default camera configuration file: " << cameraFile << std::endl; }
    }

    if (result.count("i")) {
        imageFile = result["i"].as<std::string>();
    }

    if (DEBUG) { std::cerr << "Loading image file: " << imageFile << std::endl; } 

    return 0;
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
    cxxopts::Options options("multimarker-detection", "Marker detection sample program using ARToolKitPlus library.");
    options.add_options()
        ("d, debug", "Enable debug mode. This will print helpfull process informations on the standard error stream.")
        ("c, camera-calibration", "The camera calibration file that will be used to adjust the results depending on the physical camera characteristics.", cxxopts::value<std::string>())
        ("i, in-file", "The image file to detect marker on.", cxxopts::value<std::string>())
        ("h, help", "Print this help message.");

    int result = parseCommandLine(options, argc, argv);

    uint width = 340, height = 220;
    unsigned char* data = loadImage(imageFile, width, height);

    TrackerMultiMarker* tracker = new TrackerMultiMarker(width, height, 8, 6, 6, 6, 0);
    tracker->setPixelFormat(ARToolKitPlus::PIXEL_FORMAT_LUM);
    bool init = tracker->init(cameraFile.c_str(), "../data/markerboard_480-499.cfg", 1.0f, 1000.0f);
    if (!init)
    {
        if (DEBUG) { std::cerr << "Could not initialize Tracker" << std::endl; }
        return EXIT_FAILURE;
    }

    if (DEBUG) {
        tracker->getCamera()->printSettings();
    }

    /* Marker detection options */
    tracker->setThreshold(160);
    tracker->setMarkerMode(ARToolKitPlus::MARKER_ID_SIMPLE);
    tracker->setBorderWidth(0.125); // BCH markers
    tracker->setUndistortionMode(ARToolKitPlus::UNDIST_LUT);
    
    tracker->calc(data);

    int markersCount = tracker->getNumDetectedMarkers();
    if (DEBUG) {
        std::cerr << "Found " << markersCount <<  " ARToolKitPlus markers." << std::endl;
    }
}