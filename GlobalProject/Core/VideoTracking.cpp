

#include <iostream>
#include <Box2D/Collision/Shapes/b2Shape.h>  // for shape types (can remove layter probably)
#include "VideoTracking.hpp"

VideoTracking::VideoTracking()
: m_orbMatcher(cv::NORM_HAMMING, true)
//nfeatures=500, scaleFactor=1.2f, nlevels=8, edgeThreshold=31, firstLevel=0, WTA_K=2, scoreType=ORB::HARRIS_SCORE, patchSize=31
, m_orbFeatureEngine(500, 1.2f, 8, 31, 0, 2, cv::ORB::HARRIS_SCORE, 31)
// TODO: adjust parameters for object where applicable
, m_surfDetector()
, m_surfExtractor()
, m_surfMatcher()
, m_siftEngine()
{
    // Instantiate a matcher for descriptor based correlation of features.
    this->m_matcher = new cv::FlannBasedMatcher(new cv::flann::LshIndexParams(5, 24, 2));
    this->m_2DWorld = new World();
    this->m_featureTypeForDetection = FeatureType::ORB;
    this->m_useGoodPointsOnly = false;  // indicates that should only use "good points" (3 time the min distnace) for homography calculation.

}

VideoTracking::~VideoTracking()
{
    delete this->m_matcher;
    delete this->m_2DWorld;
}

void VideoTracking::getGray(const cv::Mat& input, cv::Mat& gray)
{
    const int numChannes = input.channels();

    if (numChannes == 4)
    {
        cv::cvtColor(input, gray, CV_BGRA2GRAY);
    }
    else if (numChannes == 3)
    {
        cv::cvtColor(input, gray, CV_BGR2GRAY);
    }
    else if (numChannes == 1)
    {
        gray = input;
    }
}

// get the world simulation member of the tracking
World* VideoTracking::getWorld(){
    return this->m_2DWorld;
}

void VideoTracking::setFeatureType(FeatureType feat_type)
{
    this->m_featureTypeForDetection = feat_type;
}

//! Processes a frame and returns output image
bool VideoTracking::processFrame(const cv::Mat& inputFrame, cv::Mat& outputFrame)
{   
    this->m_2DWorld->update(m_refFrame2CurrentHomography);

    if(this->m_2DWorld->isAllObjectsDestroyed()){
        this->notifyObjectsDestryedObservers();
    }

    inputFrame.copyTo(outputFrame);

    getGray(inputFrame, m_nextImg);

    switch (m_featureTypeForDetection)
    {
        case FeatureType::SIFT:
            m_siftEngine(m_nextImg, cv::Mat(), m_nextKeypoints, m_nextDescriptors);
            break;
        case FeatureType::SURF:
            // detect key points
            m_surfDetector.detect(m_nextImg, m_nextKeypoints);
            // extract features corresponding to the key points
            m_surfExtractor.compute(m_nextImg, m_nextKeypoints, m_nextDescriptors);
            break;
        case FeatureType::ORB:
            m_orbFeatureEngine(m_nextImg, cv::Mat(), m_nextKeypoints, m_nextDescriptors);
            break;
    }
    
    calcHomographyAndTransformScene(outputFrame);

    if (this->m_2DWorld->isDebugDrawEnabled()){
        // add the debug drawing
        this->m_2DWorld->getDebugDraw()->SetScene(outputFrame);
        this->m_2DWorld->getWorld()->DrawDebugData();
    }

    return true;
}

// Sets the reference frame for latter processing
void VideoTracking::setReferenceFrame(const cv::Mat& reference)
{
    // save the reference frame
    reference.copyTo(m_refFrame);

    // get a gray image
    getGray(m_refFrame, m_refFrame);

    // detect key points and generate descriptors for the reference frame
    switch (m_featureTypeForDetection)
    {
        case FeatureType::SIFT:
            m_siftEngine(m_refFrame, cv::Mat(), m_refKeypoints, m_refDescriptors);
            break;
        case FeatureType::SURF:
            // detect key points
            m_surfDetector.detect(m_refFrame, m_refKeypoints);
            // extract features corresponding to the key points
            m_surfExtractor.compute(m_refFrame, m_refKeypoints, m_refDescriptors);
            break;
        case FeatureType::ORB:
            m_orbFeatureEngine(m_refFrame, cv::Mat(), m_refKeypoints, m_refDescriptors);
            break;
    }

    // create the scene and draw square and borders in it
    m_scene.create(reference.rows, reference.cols, CV_8UC3);
    m_scene = BLACK_COLOR;

    // draw a square in the center
    rectangle(m_scene, cvPoint(reference.cols/2 - SCENE_OFFSET, reference.rows/2 + 10), cvPoint(reference.cols/2 + SCENE_OFFSET, reference.rows /2 - 10), cv::Scalar(0,255,255), -1);

    // draw a border SCENE_OFFSET pixels into the image
    line(m_scene, cvPoint(SCENE_OFFSET, SCENE_OFFSET), cvPoint(SCENE_OFFSET, reference.rows - SCENE_OFFSET), GREEN_COLOR, 2);
    line(m_scene, cvPoint(SCENE_OFFSET, reference.rows - SCENE_OFFSET), cvPoint(reference.cols - SCENE_OFFSET, reference.rows - SCENE_OFFSET), GREEN_COLOR, 2);
    line(m_scene, cvPoint(reference.cols - SCENE_OFFSET, reference.rows - SCENE_OFFSET), cvPoint(reference.cols - SCENE_OFFSET, SCENE_OFFSET), GREEN_COLOR, 2);
    line(m_scene, cvPoint(reference.cols - SCENE_OFFSET, SCENE_OFFSET), cvPoint(SCENE_OFFSET, SCENE_OFFSET), GREEN_COLOR, 2);

    this->m_2DWorld->initializeWorldOnFirstFrame(reference, this->isRestrictBallInScene());

}

bool isPointInScene (cv::Point2f point, int width, int height){
    return 0 < point.x && point.x < width && 0 < point.y && point.y < height;
}

// calculate homography for keypoint/descriptors based tracking,
// find matching (corresponding) features (using open CV built in matcher)
// calculate the homograpgy (using open CV built in function that uses RANSAC)
// then transform the scene and add it to the output frame
void VideoTracking::calcHomographyAndTransformScene(cv::Mat& outputFrame)
{
    // transform the sence using descriptors, correspondance and homography
    if (m_refKeypoints.size() > 3)
    {
        // create vector for matches and find matches between reference and new descriptors
        std::vector<cv::DMatch> matches;

        //std::cout << "There are " << m_refKeypoints.size() << " refs and " << m_nextKeypoints.size() << " new" << std::endl;

        // m_refDescriptors is keypoint descriptors from the first (reference) frame
        // m_nextDescriptors is keypoint descriptors from the current processed frame
        switch (this->m_featureTypeForDetection)
        {
            case FeatureType::ORB:
                m_matcher->match(m_refDescriptors, m_nextDescriptors, matches);
                break;
            case FeatureType::SIFT:
            case FeatureType::SURF:
                m_surfMatcher.match(m_refDescriptors, m_nextDescriptors, matches);
            default:
                break;
        }
        
        // std::cout << "Found " << matches.size() << " matches" << std::endl;

        if (this->m_useGoodPointsOnly)
        {            
            double max_dist = 0; double min_dist = 100;

            // Quick calculation of max and min distances between keypoints
            for( int i = 0; i < matches.size(); i++ )
            {
                double dist = matches[i].distance;
                if( dist < min_dist ) min_dist = dist;
                if( dist > max_dist ) max_dist = dist;
            }

            // std::cout << "-- Max dist : " << max_dist << std::endl;
            // std::cout << "-- Min dist : " << min_dist << std::endl;

            // Draw only "good" matches (i.e. whose distance is less than 3*min_dist )
            std::vector< cv::DMatch > good_matches;

            // min dist could be 0 so make it slightly larger if zero
            min_dist = std::max(min_dist, 0.05);

            for( int i = 0; i < matches.size(); i++ )
            {
                if( matches[i].distance < 3 * min_dist ) {
                    good_matches.push_back( matches[i]);
                }
            }

            good_matches.swap(matches);
        }

        // Localize the object
        std::vector<cv::Point2f> refPoints, newPoints;

        for( int i = 0; i < matches.size(); i++ )
        {
            // Get the keypoints from the good matche (reference and new)
            refPoints.push_back(m_refKeypoints[matches[i].queryIdx].pt);
            newPoints.push_back(m_nextKeypoints[matches[i].trainIdx].pt);
        }

        // std::cout << refPoints.size() << " good matches" << std::endl;

        if (refPoints.size() > 3 && newPoints.size() > 3)
        {
            // finally, find the homography
            this->m_refFrame2CurrentHomography = findHomography(refPoints, newPoints, CV_RANSAC);
            b2Vec2 ballPosition = this->m_2DWorld->getBallBody()->GetPosition();
            if(!this->isRestrictBallInScene()){

                cv::Point2f pointToCheck = CVUtils::transformPoint(cv::Point2f(ballPosition.x * PTM_RATIO, ballPosition.y * PTM_RATIO), this->m_refFrame2CurrentHomography);

                if(!isPointInScene(pointToCheck, outputFrame.cols, outputFrame.rows)){
                    this->notifyBallInSceneObservers();
                    return;
                }
            }

            // wrap/transform the scene
            cv::Mat transformedScene;
            m_scene.copyTo(transformedScene);
            std::vector<cv::Point2f*> guardLocations = this->m_2DWorld->getGuardLocations();
            for (int i = 0; i < (int)guardLocations.size(); i++)
            {
                cv::circle(transformedScene, *guardLocations[i], 5, cv::Scalar(200, 200, 200), -1);
            }

            cv::Mat mask_image(outputFrame.size(), CV_8U, BLACK_COLOR);
            std::vector<cv::Point*> destroyedPolygons = this->m_2DWorld->getDestroyedPolygons();
            std::vector<int> destroyedPolygonsPointCount = this->m_2DWorld->getDestroyedPolygonsPointCount();
            for (int i = 0; i < destroyedPolygons.size(); i++)
            {
                const cv::Point* ppt[1] = { destroyedPolygons[i] };
                int npt[] = { destroyedPolygonsPointCount[i] };
                fillPoly(mask_image, ppt, npt, 1, WHITE_COLOR);
            }

            // now that the mask is prepared, copy the points from the inpainted to the scene
            m_inpaintedScene.copyTo(transformedScene, mask_image);
            
            cv::circle(transformedScene,
                       cv::Point2f(ballPosition.x * PTM_RATIO, ballPosition.y * PTM_RATIO),
                       26, BLUE_COLOR, -1);
            
            warpPerspective(transformedScene, transformedScene, this->m_refFrame2CurrentHomography, outputFrame.size(), CV_INTER_LINEAR);

            // add to the output
//            outputFrame += transformedScene;
            transformedScene.copyTo(outputFrame, transformedScene);
        }
    }
}




void VideoTracking::onPanGestureEnded(std::vector<cv::Point> touchPoints){
    for (std::vector<cv::Point>::iterator it = touchPoints.begin() ; it != touchPoints.end(); ++it){
        this->onMouse(CV_EVENT_LBUTTONDOWN, it->x, it->y, NULL, NULL);
    }
}

// Handle mouse event adding a body to the worlds at the mouse/touch location.
void VideoTracking::onMouse( int event, int x, int y, int, void* )
{
    if (event != CV_EVENT_LBUTTONDOWN || this->m_refFrame2CurrentHomography.empty()){
        return;
    }

    cv::Point2f targetPoint = CVUtils::transformPoint(cv::Point2f(x, y), this->m_refFrame2CurrentHomography.inv());

    this->m_2DWorld->createNewPhysicPointInWorld(targetPoint);
    
}

// Static callback for allowing mouse events registration in Open CV window.
void VideoTracking::mouseCallback(int event, int x, int y, int flags, void *param)
{
    VideoTracking *self = static_cast<VideoTracking*>(param);
    self->onMouse(event, x, y, flags, param);
}


void VideoTracking::prepareInPaintedScene(const cv::Mat scene, const std::vector<std::vector<cv::Point>> contours)
{
    LcInPaint inpaint;
    inpaint.inpaint(scene, contours, m_inpaintedScene);
}


void VideoTracking::delegateBallHitObserver(std::function<void(float x, float y)> func)
{
    this->m_2DWorld->attachBallHitObserver(func);
}

void VideoTracking::attachObjectsDestryedObserver(std::function<void()> func)
{
    objectsDestroyedObserversList.push_back(func);
}

void VideoTracking::detachObjectsDestryedObserver(std::function<void ()> func)
{
    //    objectsDestroyedObserversList.erase(std::remove(objectsDestroyedObserversList.begin(), objectsDestroyedObserversList.end(), func), objectsDestroyedObserversList.end());
}

void VideoTracking::notifyObjectsDestryedObservers()
{
    for(std::vector<std::function<void()>>::const_iterator iter = objectsDestroyedObserversList.begin(); iter != objectsDestroyedObserversList.end(); ++iter)
    {
        if(*iter != 0)
        {
            (*iter)();
        }
    }
}

void VideoTracking::attachBallInSceneObserver(std::function<void ()> func)
{
    ballInSceneObserversList.push_back(func);
}

void VideoTracking::detachBallInSceneObserver(std::function<void ()> func)
{
    //    ballInSceneObserversList.erase(std::remove(ballInSceneObserversList.begin(), ballInSceneObserversList.end(), func), ballInSceneObserversList.end());
}

void VideoTracking::notifyBallInSceneObservers()
{
    for(std::vector<std::function<void()>>::const_iterator iter = ballInSceneObserversList.begin(); iter != ballInSceneObserversList.end(); ++iter)
    {
        if(*iter != 0)
        {
            (*iter)();
        }
    }
}

void VideoTracking::setRestrictBallInScene(bool restricted){
    this->m_restrictBallInScene = restricted;
}

bool VideoTracking::isRestrictBallInScene(){
    return this->m_restrictBallInScene;
}
