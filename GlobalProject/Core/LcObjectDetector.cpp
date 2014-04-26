#include "LcObjectDetector.h"


LcObjectDetector::LcObjectDetector(void)
{
}


LcObjectDetector::~LcObjectDetector(void)
{
}

std::vector<std::vector<cv::Point>> LcObjectDetector::getObjectContours(const cv::Mat &image)
{
    std::vector<std::vector<cv::Point>> contours;
    std::vector<std::vector<cv::Point>> objectContours;
	cv::Mat output;
    cv::Canny(image, output, 100, 200);//; showWatershedSegmentation(frame2);

    // try to find countors on the image
    cv::findContours(output, contours, CV_RETR_LIST, CV_CHAIN_APPROX_NONE);

    std::vector<cv::Point> approx;
    // test each contour
    for( size_t i = 0; i < contours.size(); i++ )
    {
        approxPolyDP(cv::Mat(contours[i]), approx, cv::arcLength(cv::Mat(contours[i]), true)*0.02, true);
                        
        if (approx.size() == 4 &&
            fabs(cv::contourArea(cv::Mat(approx))) > 1000 &&
            cv::isContourConvex(cv::Mat(approx)) )
        {
            objectContours.push_back(std::vector<cv::Point>(approx));
        }
        else
        {
            convexHull(contours[i], approx); 
			if (fabs(cv::contourArea(cv::Mat(approx))) > 1000){
				// limit the size
				objectContours.push_back(std::vector<cv::Point>(approx));
			}
        }
    }

    return objectContours;
}
