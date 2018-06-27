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

// Custom redis image helper library
#include <RedisImageHelper.hpp>

// Opencv (required by chilitags)
#include <opencv2/opencv.hpp>

// Used in exists function to access file.
// /!\ Uses POSIX functions
#include <sys/stat.h>

static const int ARTK = 0;
static const int CTAG = 1;

bool VERBOSE = false;
bool STREAM_MODE = true;
std::string redisInputKey = "custom:image";
std::string redisOutputKey = "custom:image:output";
std::string redisInputCameraParametersKey = "default:camera:parameters";
std::string redisHost = "127.0.0.1";
std::string cameraCalibrationFile = "../data/no_distortion.cal";

int redisPort = 6379;
int markerType = ARTK;

using ARToolKitPlus::TrackerMultiMarker;
using chilitags::Chilitags;

struct contextData {
    uint width;
    uint height;
    uint channels;
    TrackerMultiMarker* ARTKTracker;
    RedisImageHelperSync* clientSync;
};

static bool exists(std::string filename) {
    struct stat buffer;
    return (stat(filename.c_str(), &buffer) == 0);
}

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
        std::string fileName = result["c"].as<std::string>();
        if (exists(fileName)) {
            cameraCalibrationFile = fileName;
            if (VERBOSE) {
                std::cerr << "Camera file specified. Using " << cameraCalibrationFile << " as camera calibration file." << std::endl;
            }
        }
        else {
            if (VERBOSE) {
                std::cerr << "Specified camera file could not be found. Using default " << cameraCalibrationFile << " as camera calibration file." << std::endl;
            }
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

    if (result.count("m")) {
        markerType = result["m"].as<int>();
        if (VERBOSE) {
            std::cerr << "Marker type was set to " << (markerType == CTAG ? "`Chillitags`" : "`ARToolKit`") << std::endl;
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

    if (result.count("camera-parameters")) {
        redisInputCameraParametersKey = result["camera-parameters"].as<std::string>();
        if (VERBOSE) {
            std::cerr << "Camera parameters output key was set to " << redisInputCameraParametersKey << std::endl;
        }
    }
    else {
        if (VERBOSE) {
            std::cerr << "No camera parameters output key specified. Camera parameters output key was set to " << redisInputCameraParametersKey << std::endl;
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

static TrackerMultiMarker* createARTKTracker(uint width, uint height) {
    TrackerMultiMarker* tracker = new TrackerMultiMarker(width, height, 20, 6, 6, 6, 20);
    tracker->setPixelFormat(ARToolKitPlus::PIXEL_FORMAT_LUM);
    bool init = tracker->init(cameraCalibrationFile.c_str(), "../data/markerboard_480-499.cfg", 1.0f, 1000.0f);
    if (!init)
    {
        if (VERBOSE) {
            std::cerr << "Could not initialize ARToolKit TrackerMultiMarker." << std::endl;
        }
        return NULL;
    }

    if (VERBOSE) {
        tracker->getCamera()->printSettings();
    }

    /* Marker detection options */
    tracker->activateAutoThreshold(true);
    tracker->setMarkerMode(ARToolKitPlus::MARKER_ID_BCH);
    tracker->setPixelFormat(ARToolKitPlus::PIXEL_FORMAT_LUM);
    tracker->setBorderWidth(0.125f); // BCH markers
    tracker->setUndistortionMode(ARToolKitPlus::UNDIST_NONE);
    tracker->setImageProcessingMode(ARToolKitPlus::IMAGE_FULL_RES);
    tracker->setUseDetectLite(false);

    return tracker;
}

static void detectARTKMarkers(TrackerMultiMarker* tracker, unsigned char* grayImage) {
    tracker->calc(grayImage);
}

static float** refineCorners(cv::Mat image, const float vertex[8][2]) {
    std::vector<cv::Point2f> corners;
    for (int j = 0; j < 4; j++) {
        corners.push_back(cv::Point2f(vertex[j][0], vertex[j][1]));
    }

    int subPixelWindow = 11;
    float halfSubPixelWindow = subPixelWindow/2;
    cv::Size subPixelSize = cv::Size(halfSubPixelWindow, halfSubPixelWindow);
    cv::Size subPixelZeroZone = cv::Size(-1, -1);
    cv::TermCriteria subPixelTermCriteria = cv::TermCriteria(CV_TERMCRIT_EPS, 100, 0.001);

    int width = image.cols;
    int height = image.rows;

    float** new_vertex = new float*[4];
    for(int i = 0 ; i < 4 ; ++i) {
        new_vertex[i] = new float[2];
        new_vertex[i][0] = vertex[i][0];
        new_vertex[i][1] = vertex[i][1];
    }

    int w = (subPixelWindow/2) + 1;
    if (vertex[0][0] - w < 0        || vertex[0][0] + w >= width    || vertex[0][1] - w < 0     || vertex[0][1] + w >= height ||
            vertex[1][0] - w < 0    || vertex[1][0] + w >= width    || vertex[1][1] - w < 0     || vertex[1][1] + w >= height ||
            vertex[2][0] - w < 0    || vertex[2][0] + w >= width    || vertex[2][1] - w < 0     || vertex[2][1] + w >= height ||
            vertex[3][0] - w < 0    || vertex[3][0] + w >= width    || vertex[3][1] - w < 0     || vertex[3][1] + w >= height) {
        // too tight
        return new_vertex;
    }

    CvBox2D box = cv::minAreaRect(corners);
    float bw = box.size.width;
    float bh = box.size.height;
    if (bw <= 0 || bh <= 0 || bw/bh < 0.1 || bw/bh > 10) {
        // marker is too "flat" to have been IDed correctly...
        return new_vertex;
    }

    cv::cornerSubPix(image, corners, subPixelSize, subPixelZeroZone, subPixelTermCriteria);
    for (int i = 0 ; i < 4 ; i++) {
        new_vertex[i][0] = corners.at(i).x;
        new_vertex[i][1] = corners.at(i).y;
    }
    return new_vertex;
}

static rapidjson::Value* ARTKMarkerToJSON(cv::Mat image, const ARToolKitPlus::ARMarkerInfo& markerInfo, rapidjson::Document::AllocatorType& allocator)
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

    float** vertex = refineCorners(image, markerInfo.vertex);

    rapidjson::Value cornerArray (rapidjson::kArrayType);
    // WARNING: corners should be pushed in the following order:
    // top left - top right - bot right - bot left
    cornerArray.PushBack(vertex[3][0], allocator);
    cornerArray.PushBack(vertex[3][1], allocator);

    cornerArray.PushBack(vertex[0][0], allocator);
    cornerArray.PushBack(vertex[0][1], allocator);

    cornerArray.PushBack(vertex[1][0], allocator);
    cornerArray.PushBack(vertex[1][1], allocator);

    cornerArray.PushBack(vertex[2][0], allocator);
    cornerArray.PushBack(vertex[2][1], allocator);

    // Filling the marker obj with the corners data
    markerObj->AddMember("corners", cornerArray, allocator);
    return markerObj;
}

static rapidjson::Value* ARTKMarkersToJSON(cv::Mat image, TrackerMultiMarker* ARTKTracker, rapidjson::Document::AllocatorType& allocator) {
    rapidjson::Value* markersObj = new rapidjson::Value(rapidjson::kArrayType);
    int markersCount = ARTKTracker->getNumDetectedMarkers();
    if (VERBOSE) {
        std::cerr << "Found " << markersCount <<  " ARToolKitPlus markers." << std::endl;
    }

    for(int i = 0 ; i < markersCount ; i++) {
        auto markerInfo = ARTKTracker->getDetectedMarker(i);
        // Converting markerInfo to rapidjson obj
        rapidjson::Value* markerObj = ARTKMarkerToJSON(image, markerInfo, allocator);

        // Filling the markers with the generated marker object
        markersObj->PushBack(*markerObj, allocator);
        delete markerObj;
    }
    return markersObj;
}

static chilitags::TagCornerMap* detectCTags(unsigned char* grayImage, uint width, uint height)
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

static rapidjson::Value* CTagsToJSON(chilitags::TagCornerMap* tags, rapidjson::Document::AllocatorType& allocator) {
    rapidjson::Value* tagsObj = new rapidjson::Value(rapidjson::kArrayType);
    int markersCount = tags->size();
    if (VERBOSE) {
        std::cerr << "Found " << markersCount << " Chilitags markers." << std::endl;
    }

    for (const std::pair<int, chilitags::Quad>& tag : *tags)
    {
        rapidjson::Value* tagObj = CTagToJSON(tag, allocator);
        tagsObj->PushBack(*tagObj, allocator);
        delete tagObj;
    }
    return tagsObj;
}

void onImagePublished(redisAsyncContext* c, void* rep, void* privdata) {
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
    TrackerMultiMarker* ARTKTracker = data->ARTKTracker;
    RedisImageHelperSync* clientSync = data->clientSync;

    Image* image = RedisImageHelper::dataToImage(reply->element[2]->str, width, height, channels, reply->element[2]->len);
    if (image == NULL) {
        if (VERBOSE) {
            std::cerr << "Could not retrieve image from data." << std::endl;
        }
        return;
    }

    unsigned char* grayData = rgb_to_gray(width, height, image->data());

    // Creating JSON data structure that will hold markers information.
    rapidjson::Document jsonMarkers;
    jsonMarkers.SetObject();
    rapidjson::Document::AllocatorType& allocator = jsonMarkers.GetAllocator();

    if (markerType == ARTK) {
        cv::Mat opencvImage(width, height, CV_8UC1, grayData);
        detectARTKMarkers(ARTKTracker, grayData);

        // markersObj will be the array holding each individual marker objects.
        rapidjson::Value* markersObj = ARTKMarkersToJSON(opencvImage, ARTKTracker, allocator);

        // Finally putting everything on the document object
        jsonMarkers.AddMember("markers", *markersObj, allocator);
    }
    else if (markerType == CTAG)
    {
        chilitags::TagCornerMap* map = detectCTags(grayData, width, height);

        // markersObj will be the array holding each individual marker objects.
        rapidjson::Value* markersObj = CTagsToJSON(map, allocator);

        // Finally putting everything on the document object
        jsonMarkers.AddMember("markers", *markersObj, allocator);
    }
    rapidjson::StringBuffer strbuf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
    jsonMarkers.Accept(writer);

    clientSync->publishString((char*)strbuf.GetString(), redisOutputKey);
    if (VERBOSE) {
        std::cerr << strbuf.GetString() << std::endl;
    }
    delete[] grayData;
    delete image;
}

int main(int argc, char** argv)
{
    cxxopts::Options options("markers-detection-server", "Marker detection sample program using ARToolKitPlus library & redis.");
    options.add_options()
            ("redis-port", "The port to which the redis client should try to connect.", cxxopts::value<int>())
            ("redis-host", "The host adress to which the redis client should try to connect", cxxopts::value<std::string>())
            ("i, input", "The redis input key where data are going to arrive.", cxxopts::value<std::string>())
            ("o, output", "The redis output key where to set output data.", cxxopts::value<std::string>())
            ("s, stream", "Activate stream mode. In stream mode the program will constantly process input data and publish output data. By default stream mode is enabled.")
            ("u, unique", "Activate unique mode. In unique mode the program will only read and output data one time.")
            ("m, marker-type", "The type of the marker to use. (0) ARTK ; (1) Chilitags.", cxxopts::value<int>()) //TODO
            ("c, camera-calibration", "The camera calibration file that will be used to adjust the results depending on the physical camera characteristics.", cxxopts::value<std::string>())
            ("camera-parameters", "The redis input key where camera-parameters are going to arrive.", cxxopts::value<std::string>())
            ("v, verbose", "Enable verbose mode. This will print helpfull process informations on the standard error stream.")
            ("h, help", "Print this help message.");

    int retCode = parseCommandLine(options, argc, argv);
    if (retCode)
    {
        return EXIT_FAILURE;
    }

    RedisImageHelperSync clientSync(redisHost, redisPort, redisInputKey);
    if (!clientSync.connect()) {
        std::cerr << "Cannot connect to redis server. Please ensure that a redis server is up and running." << std::endl;
        return EXIT_FAILURE;
    }

    struct contextData data;
    data.width = clientSync.getInt(redisInputCameraParametersKey + ":width");
    data.height = clientSync.getInt(redisInputCameraParametersKey + ":height");
    data.channels = clientSync.getInt(redisInputCameraParametersKey + ":channels");
    if (data.width == -1 || data.height == -1 || data.channels == -1) {
        std::cerr << "Could not find camera parameters (width height channels). Please specify where to find them in redis with the --camera-parameters option parameters." << std::endl;
        return EXIT_FAILURE;
    }

    TrackerMultiMarker* tracker = createARTKTracker(data.width, data.height);
    data.ARTKTracker = tracker;
    data.clientSync = &clientSync;

    if (STREAM_MODE) {
        // In stream mode we need another client that will subscribe to the input key channel.
        RedisImageHelperAsync clientAsync(redisHost, redisPort, redisInputKey);
        if (!clientAsync.connect()) {
            std::cerr << "Cannot connect to redis server. Please ensure that a redis server is up and running." << std::endl;
            return EXIT_FAILURE;
        }
        clientAsync.subscribe(redisInputKey, onImagePublished, static_cast<void*>(&data));
    }
    else {
        Image* image = clientSync.getImage(data.width, data.height, data.channels, redisInputKey);
        if (image == NULL) {
            if (VERBOSE) {
                std::cerr << "Could not fetch image data from redis server. Please ensure that the key you provided is correct." << std::endl;
            }
            return EXIT_FAILURE;
        }

        unsigned char* grayData = rgb_to_gray(data.width, data.height, image->data());
        cv::Mat opencvImage(image->width(), image->height(), CV_8UC1, grayData);
        detectARTKMarkers(data.ARTKTracker, grayData);

        // Creating JSON data structure that will hold markers information.
        rapidjson::Document jsonMarkers;
        jsonMarkers.SetObject();
        rapidjson::Document::AllocatorType& allocator = jsonMarkers.GetAllocator();
        // markersObj will be the array holding each individual marker objects.
        rapidjson::Value* markersObj = ARTKMarkersToJSON(opencvImage, data.ARTKTracker, allocator);

        // Finally putting everything on the document object
        jsonMarkers.AddMember("markers", *markersObj, allocator);

        rapidjson::StringBuffer strbuf;
        rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
        jsonMarkers.Accept(writer);

        clientSync.setString((char*)strbuf.GetString(), redisOutputKey);
        if (VERBOSE) {
            std::cerr << strbuf.GetString() << std::endl;
        }
        delete image;
    }
    return EXIT_SUCCESS;
}
