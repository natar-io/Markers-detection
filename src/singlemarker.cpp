#include <iostream>
#include <fstream>

#include <ARToolKitPlus/TrackerSingleMarker.h>
#include <cxxopts.hpp>

bool DEBUG = false;

using ARToolKitPlus::TrackerSingleMarker;

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
            ("i, in-file", "The image file to detect marker on", cxxopts::value<std::string>())
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

    if (result.count("i"))
    {
        imageFile = result["i"].as<std::string>();
    }
    else {
        if (DEBUG) { std::cerr << "No image test file specified. Exiting..." << std::endl; }
        return EXIT_FAILURE;
    }

    unsigned char* data = loadImage(imageFile, 320, 240);

    TrackerSingleMarker tracker(320, 240);
    tracker.setPixelFormat(ARToolKitPlus::PIXEL_FORMAT_LUM);
    bool init = tracker.init(cameraFile.c_str(), 1.0f, 1000.0f);
    if (!init)
    {
        if (DEBUG) { std::cerr << "Could not initialize Tracker" << std::endl; }
        return EXIT_FAILURE;
    }

    if (DEBUG) { tracker.getCamera()->printSettings(); }
    tracker.setMarkerMode(ARToolKitPlus::MARKER_ID_BCH);
    tracker.setBorderWidth(0.125); // BCH markers
    tracker.setUndistortionMode(ARToolKitPlus::UNDIST_LUT);

    std::vector<int> markersId = tracker.calc(data);
    tracker.selectBestMarkerByCf();
    float conf = tracker.getConfidence();

    if (DEBUG) {
        std::cerr << "Found marker " << markersId[0] << std::endl << "\tConfidence: " << int(conf * 100.0) << "%" << std::endl;
    }

}
