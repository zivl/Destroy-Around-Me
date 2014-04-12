#include "GrabCutSegmenter.h"


GrabCutSegmenter::GrabCutSegmenter(void)
{
}


GrabCutSegmenter::~GrabCutSegmenter(void)
{
}

void GrabCutSegmenter::reset()
{
    if( !mask.empty() ){
        mask.setTo(Scalar::all(GC_BGD));
	}
    bgdPxls.clear(); fgdPxls.clear();
    prBgdPxls.clear();  prFgdPxls.clear();

    isInitialized = false;
    rectState = NOT_SET;
    lblsState = NOT_SET;
    prLblsState = NOT_SET;
    iterCount = 0;
}

void GrabCutSegmenter::setImageAndWinName( const Mat& _image, const string& _winName  )
{
    if( _image.empty() || _winName.empty() )
        return;
    image = &_image;
    winName = &_winName;
    mask.create( image->size(), CV_8UC1);
    reset();
}

void GrabCutSegmenter::showImage() const
{
    if( image->empty() || winName->empty() ){
        return;
	}

    Mat res;
    Mat binMask;
    if( !isInitialized ){
        image->copyTo( res );
	}
    else {
        OpenCVUtils::getBinMask( mask, binMask );
        image->copyTo( res, binMask );
    }

    vector<Point>::const_iterator it;
    for( it = bgdPxls.begin(); it != bgdPxls.end(); ++it ){
        circle( res, *it, radius, BLUE, thickness );
	}
    for( it = fgdPxls.begin(); it != fgdPxls.end(); ++it ){
        circle( res, *it, radius, RED, thickness );
	}
    for( it = prBgdPxls.begin(); it != prBgdPxls.end(); ++it ){
        circle( res, *it, radius, LIGHTBLUE, thickness );
	}
    for( it = prFgdPxls.begin(); it != prFgdPxls.end(); ++it ){
        circle( res, *it, radius, PINK, thickness );
	}

    if( rectState == IN_PROCESS || rectState == SET ){
		rectangle( res, Point( rect.x, rect.y ), Point(rect.x + rect.width, rect.y + rect.height ), GREEN, 2);
	}

	imshow( *winName, res );
}

void GrabCutSegmenter::setRectInMask()
{
    CV_Assert( !mask.empty() );
    mask.setTo( GC_BGD );
    rect.x = max(0, rect.x);
    rect.y = max(0, rect.y);
    rect.width = min(rect.width, image->cols-rect.x);
    rect.height = min(rect.height, image->rows-rect.y);
    (mask(rect)).setTo( Scalar(GC_PR_FGD) );
}

void GrabCutSegmenter::setLblsInMask( int flags, Point p, bool isPr )
{
    vector<Point> *bpxls, *fpxls;
    uchar bvalue, fvalue;
    if( !isPr )
    {
        bpxls = &bgdPxls;
        fpxls = &fgdPxls;
        bvalue = GC_BGD;
        fvalue = GC_FGD;
    }
    else
    {
        bpxls = &prBgdPxls;
        fpxls = &prFgdPxls;
        bvalue = GC_PR_BGD;
        fvalue = GC_PR_FGD;
    }
    if( flags & BGD_KEY )
    {
        bpxls->push_back(p);
        circle( mask, p, radius, bvalue, thickness );
    }
    if( flags & FGD_KEY )
    {
        fpxls->push_back(p);
        circle( mask, p, radius, fvalue, thickness );
    }
}

void GrabCutSegmenter::mouseClick( int event, int x, int y, int flags, void* )
{
    // TODO add bad args check
    switch( event )
    {
    case EVENT_LBUTTONDOWN: // set rect or set GrabCut Background/Foreground labels
        {
            bool isb = (flags & BGD_KEY) != 0,
                 isf = (flags & FGD_KEY) != 0;
            if( rectState == NOT_SET && !isb && !isf )
            {
                rectState = IN_PROCESS;
                rect = Rect( x, y, 1, 1 );
            }
            if ( (isb || isf) && rectState == SET )
                lblsState = IN_PROCESS;
        }
        break;
    case EVENT_RBUTTONDOWN: // set GrabCut Probably Background/Foreground labels
        {
            bool isb = (flags & BGD_KEY) != 0,
                 isf = (flags & FGD_KEY) != 0;
            if ( (isb || isf) && rectState == SET )
                prLblsState = IN_PROCESS;
        }
        break;
    case EVENT_LBUTTONUP:
        if( rectState == IN_PROCESS )
        {
            rect = Rect( Point(rect.x, rect.y), Point(x,y) );
            rectState = SET;
            setRectInMask();
            CV_Assert( bgdPxls.empty() && fgdPxls.empty() && prBgdPxls.empty() && prFgdPxls.empty() );
            showImage();
        }
        if( lblsState == IN_PROCESS )
        {
            setLblsInMask(flags, Point(x,y), false);
            lblsState = SET;
            showImage();
        }
        break;
    case EVENT_RBUTTONUP:
        if( prLblsState == IN_PROCESS )
        {
            setLblsInMask(flags, Point(x,y), true);
            prLblsState = SET;
            showImage();
        }
        break;
    case EVENT_MOUSEMOVE:
        if( rectState == IN_PROCESS )
        {
            rect = Rect( Point(rect.x, rect.y), Point(x,y) );
            CV_Assert( bgdPxls.empty() && fgdPxls.empty() && prBgdPxls.empty() && prFgdPxls.empty() );
            showImage();
        }
        else if( lblsState == IN_PROCESS )
        {
            setLblsInMask(flags, Point(x,y), false);
            showImage();
        }
        else if( prLblsState == IN_PROCESS )
        {
            setLblsInMask(flags, Point(x,y), true);
            showImage();
        }
        break;
    }
}

int GrabCutSegmenter::nextIter()
{
    if( isInitialized ){
        grabCut( *image, mask, rect, bgdModel, fgdModel, 1 );
	}
    else
    {
        if( rectState != SET ){
            return iterCount;
		}

        if( lblsState == SET || prLblsState == SET ){
            grabCut( *image, mask, rect, bgdModel, fgdModel, 1, GC_INIT_WITH_MASK );
		}
        else {
            grabCut( *image, mask, rect, bgdModel, fgdModel, 1, GC_INIT_WITH_RECT );
		}

        isInitialized = true;
    }
    iterCount++;

    bgdPxls.clear();
	fgdPxls.clear();
    prBgdPxls.clear();
	prFgdPxls.clear();

    return iterCount;
}

Mat GrabCutSegmenter::findSegments(Mat &image){
	//nextIter(); // TODO
	return image;
}
