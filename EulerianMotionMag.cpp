//***********************************************************************//
//
//	File Name:		EulerianMotionMag.cpp
//	Author:			Ramsundar K G (ramsundar@asu.edu)
//	Date:			December 22, 2016
//
//	Description:    EulerianMotionMag class definition
//                  Adapted from: https://github.com/wzpan/QtEVM
//
//***********************************************************************//

// Includes
#include "EulerianMotionMag.h"

// Namespace
using namespace cv;
using namespace std;


EulerianMotionMag::EulerianMotionMag()
        : input_file_name_()
        , output_file_name_()
        , input_img_width_(0)
        , input_img_height_(0)
        , input_cap_(NULL)
        , output_img_width_(0)
        , output_img_height_(0)
        , output_cap_(NULL)
        , write_output_file_(false)
        , lap_pyramid_levels_(5)
        , loop_time_ms_(0)
        , cutoff_freq_low_(0.05)    // Hz
        , cutoff_freq_high_(0.4)    // Hz
        , lambda_c_(16)
        , alpha_(20)
        , chrom_attenuation_(0.1)
        , exaggeration_factor_(2.0)
        , delta_(0)
        , lambda_(0)
        , frame_num_(0)
        , frame_count_(0)
        , input_fps_(30)
{
}

EulerianMotionMag::~EulerianMotionMag()
{
    if(input_cap_ != NULL)
        input_cap_->release();

    if(output_cap_ != NULL)
        output_cap_->release();
}

bool EulerianMotionMag::init()
{
    // Input:
    // Input File:
    input_cap_ = new cv::VideoCapture(input_file_name_);
    if (!input_cap_->isOpened())
    {
        cerr << "Error: Unable to open input video file: " << input_file_name_ << endl;
        return false;
    }

    if (input_img_width_ <= 0 || input_img_height_ <= 0)
    {
        // Use default input image size
        input_img_width_ = input_cap_->get(CV_CAP_PROP_FRAME_WIDTH);
        input_img_height_ = input_cap_->get(CV_CAP_PROP_FRAME_HEIGHT);
    }
    frame_count_ = input_cap_->get(CV_CAP_PROP_FRAME_COUNT);
    input_fps_ = input_cap_->get(CV_CAP_PROP_FPS);
    cout << "Input video resolution is (" << input_img_width_ << ", " << input_img_height_ << ")" << endl;

    // Output:
    // Output Display Window
    cvNamedWindow(DISPLAY_WINDOW_NAME, CV_WINDOW_AUTOSIZE);
    if (output_img_width_ <= 0 || output_img_height_ <= 0)
    {
        // Use input image size for output
        output_img_width_ = input_img_width_;
        output_img_height_ = input_img_height_;
    }
    cout << "Output video resolution is (" << output_img_width_ << ", " << output_img_height_ << ")" << endl;

    // Output File:
    if (!output_file_name_.empty())
        write_output_file_ = true;

    if (write_output_file_)
    {
        output_cap_ = new cv::VideoWriter(output_file_name_,// filename
                                          getCodecNumber(output_file_name_), // codec to be used
                                          input_fps_, // frame rate of the video
                                          cv::Size(output_img_width_, output_img_height_), // frame size
                                          true  // color video
                                          );
        if (!output_cap_->isOpened())
        {
            cerr << "Error: Unable to create output video file: " << output_file_name_ << endl;
            return false;
        }
    }

    cout << "Init successful!" << endl;
    return true;
}

void EulerianMotionMag::run()
{
    cout << "Running Eulter Motion Magnification program..." << endl << endl;

    while (1)
    {
        timer_.start();

        input_cap_->read(img_input_);
        if (img_input_.empty())
            break;

        cout << "Processing image frame: (" << frame_num_ << "/" << frame_count_ << ")" << flush;

        // resize input image
        resize(img_input_, img_input_, cv::Size(input_img_width_, input_img_height_));

        // 1. Convert to Lab color space
        img_input_lab_ = img_input_.clone();
        img_input_lab_.convertTo(img_input_lab_, CV_32FC3, 1.0 / 255.0f);
        cvtColor(img_input_lab_, img_input_lab_, CV_BGR2Lab);

        // 2. Spatial filtering one frame
        img_spatial_filter_ = img_input_lab_.clone();
        buildLaplacianPyramid(img_spatial_filter_, lap_pyramid_levels_, img_vec_lap_pyramid_);

        if (frame_num_ == 0)
        {
            // For first image frame
            img_vec_lowpass_1_ = img_vec_lap_pyramid_;
            img_vec_lowpass_2_ = img_vec_lap_pyramid_;
            img_vec_filtered_ = img_vec_lap_pyramid_;
        }
        else
        {
            for (int i = 0; i < lap_pyramid_levels_; ++i)
            {
                temporalIIRFilter(img_vec_lap_pyramid_[i], img_vec_filtered_[i], i);
            }

            // Amplify each spatial frequency bands, according to Figure 6 of paper
            delta_ = lambda_c_ / 8.0 / (1.0 + alpha_);

            // the factor to boost alpha_ above the bound (for better visualization)
            exaggeration_factor_ = 2.0;

            // compute the representative wavelength lambda_
            // for the lowest spatial frequency band of Laplacian pyramid
            lambda_ = sqrt((float) (input_img_width_ * input_img_width_ + input_img_height_ * input_img_height_)) / 3; // 3 is experimental constant

            for (int i = lap_pyramid_levels_; i >= 0; i--)
            {
                amplify(img_vec_filtered_[i], img_vec_filtered_[i], i);

                // go one level down on pyramid
                // representative lambda_ will reduce by factor of 2
                lambda_ /= 2.0;
            }
        }

        // 4. reconstruct motion image from img_vec_filtered_ pyramid
        reconImgFromLaplacianPyramid(img_vec_filtered_, lap_pyramid_levels_, img_motion_);

        // 5. attenuate I, Q channels
        attenuate(img_motion_, img_motion_);

        // 6. combine source frame and motion image
        if (frame_num_ > 0)    // don't amplify first frame
            img_spatial_filter_ += img_motion_;

        // 7. convert back to rgb color space and CV_8UC3
        img_motion_mag_ = img_spatial_filter_.clone();
        cvtColor(img_motion_mag_, img_motion_mag_, CV_Lab2BGR);
        img_motion_mag_.convertTo(img_motion_mag_, CV_8UC3, 255.0, 1.0 / 255.0);

        // resize output image
        resize(img_motion_mag_, img_motion_mag_, cv::Size(output_img_width_, output_img_height_));

        imshow(DISPLAY_WINDOW_NAME, img_motion_mag_);
        if (write_output_file_)
            output_cap_->write(img_motion_mag_);

        frame_num_++;
        loop_time_ms_ = timer_.getTimeMilliSec();
        cout << " | Time taken: " << loop_time_ms_ << " ms" << endl;

        char c = waitKey(1);
        if (c == 27)
            break;
    }
}

int EulerianMotionMag::getCodecNumber(string file_name)
{
    string file_extn = file_name.substr(file_name.find_last_of('.') + 1);

    // Currently supported video formats are AVI and MPEG-4
    if (file_extn == "avi")
        return CV_FOURCC('M', 'J', 'P', 'G');
    else if (file_extn == "mp4")
        return CV_FOURCC('D', 'I', 'V', 'X');
    else
        return -1;
}

Mat EulerianMotionMag::LaplacianPyr(Mat img)
{
    Mat down, up, lap;
    pyrDown(img, down);
    pyrUp(down, up);
    lap = img - up;
    return lap;
}

bool EulerianMotionMag::buildLaplacianPyramid(const Mat& img, const int levels, vector< Mat >& pyramid)
{
    if (levels < 1)
    {
        cerr << "Error: Laplacian Pyramid Levels should be larger than 1" << endl;
        return false;
    }

    pyramid.clear();
    Mat currentImg = img;
    for (int l = 0; l < levels; l++)
    {
        Mat down, up;
        pyrDown(currentImg, down);
        pyrUp(down, up, currentImg.size());
        Mat lap = currentImg - up;
        pyramid.push_back(lap);
        currentImg = down;
    }
    pyramid.push_back(currentImg);

    return true;
}

void EulerianMotionMag::reconImgFromLaplacianPyramid(const vector< Mat >& pyramid, const int levels, Mat& dst)
{
    Mat curr_img = pyramid[levels];
    for (int i = levels - 1; i >= 0; --i)
    {
        Mat up;
        pyrUp(curr_img, up, pyramid[i].size());
        curr_img = up + pyramid[i];
    }
    dst = curr_img.clone();
}

void EulerianMotionMag::temporalIIRFilter(const Mat& src, Mat& dst, int level)
{
    cv::Mat temp_1 = (1 - cutoff_freq_high_) * img_vec_lowpass_1_[level] + cutoff_freq_high_ * src;
    cv::Mat temp_2 = (1 - cutoff_freq_low_) * img_vec_lowpass_2_[level] + cutoff_freq_low_ * src;
    img_vec_lowpass_1_[level] = temp_1;
    img_vec_lowpass_2_[level] = temp_2;
    dst = img_vec_lowpass_1_[level] - img_vec_lowpass_2_[level];
}

void EulerianMotionMag::amplify(const Mat& src, Mat& dst, int level)
{
    double curr_alpha;
    //compute modified alpha_ for this level
    curr_alpha = lambda_ / delta_ / 8 - 1;
    curr_alpha *= exaggeration_factor_;
    if (level == lap_pyramid_levels_ || level == 0) // ignore the highest and lowest frequency band
        dst = src * 0;
    else
        dst = src * min(alpha_, curr_alpha);
}

void EulerianMotionMag::attenuate(Mat &src, Mat &dst)
{
    Mat planes[3];
    split(src, planes);
    planes[1] = planes[1] * chrom_attenuation_;
    planes[2] = planes[2] * chrom_attenuation_;
    merge(planes, 3, dst);
}

