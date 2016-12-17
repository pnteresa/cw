#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <iostream>
#include <string>
#include <cmath>

using namespace cv;
using namespace std;

// helper to show a matrix
void showit(Mat& it) {
  imshow("cwfloat", it);
  waitKey(0);
}

// helper to convert an angle (in radians) to [-pi, pi)
double principal_angle(double angle) {
  double tmp = fmod(angle, 2 * CV_PI); // (-2pi, 2pi)
  if (tmp < 0) {
    tmp += 2 * CV_PI;
  } // [0, 2pi)
  return fmod(tmp + CV_PI, 2 * CV_PI) - CV_PI; // [-pi, pi)
}

// gets the angle using FFT
double get_angle_fft(Mat& input) {
    // FFT
    Mat padded = input;
    int m = getOptimalDFTSize(input.rows);
    int n = getOptimalDFTSize(input.cols);
    copyMakeBorder(input, padded, 0, m - input.rows, 0, n - input.cols, BORDER_CONSTANT, Scalar::all(0));
    Mat planes[] = {Mat_<float>(padded), Mat::zeros(padded.size(), CV_32F)};
    Mat complexI;
    merge(planes, 2, complexI);
    dft(complexI, complexI);
    split(complexI, planes);
    magnitude(planes[0], planes[1], planes[0]);
    Mat magI = planes[0];
    magI += Scalar::all(1);
    log(magI, magI);
    magI = magI(Rect(0, 0, magI.cols & -2, magI.rows & -2));
    int cx = magI.cols/2;
    int cy = magI.rows/2;
    Mat q0(magI, Rect(0, 0, cx, cy));
    Mat q1(magI, Rect(cx, 0, cx, cy));
    Mat q2(magI, Rect(0, cy, cx, cy));
    Mat q3(magI, Rect(cx, cy, cx, cy));

    Mat tmp;
    q0.copyTo(tmp);
    q3.copyTo(q0);
    tmp.copyTo(q3);

    q1.copyTo(tmp);
    q2.copyTo(q1);
    tmp.copyTo(q2);

    normalize(magI, magI, 0, 1, CV_MINMAX);

    // Convert to 8-bit for remaining things
    magI.convertTo(magI, CV_8UC1, 255.);

    // Threshold
    Mat thresholded;
    threshold(magI, thresholded, 128., 255., THRESH_BINARY);

    // Get rotation
    int midr = thresholded.rows / 2;
    int midc = thresholded.cols / 2;
    int nbuckets = 100;
    int buckets[nbuckets];
    double total_angle_in_buckets[nbuckets];
    int best_bucket = 0;
    for (int i = 0; i < nbuckets; ++i) {
            buckets[i] = 0;
            total_angle_in_buckets[i] = 0.;
    }
    for (int r = 0; r < thresholded.rows; r++) {
        for (int c = 0; c < thresholded.cols; c++) {
            if ((int) thresholded.at<uchar>(r, c) < 128) {
                continue;
            }
            double angle = atan2(midr - r, midc - c);
            // We only care about the angles between -pi/4 and pi/4 (assume image is almost
            // upright).
            int angle_bucket = int(((angle * 2. / 3.14) + 0.5) * double(nbuckets));
            if (angle_bucket < 0 || angle_bucket >= nbuckets) {
                    continue;
            }
            total_angle_in_buckets[angle_bucket] += angle;
            if (++buckets[angle_bucket] > buckets[best_bucket]) {
                    best_bucket = angle_bucket;
            }
        }
    }
    // This seems OK, but the problem is that the bucket positioning could have a big effect. I
    // think something like PCA would be more sensible. Evan also thought Hough might actually work
    // if done properly.
    double rot_angle_rad = total_angle_in_buckets[best_bucket] / double(buckets[best_bucket]);
    double rot_angle_deg = rot_angle_rad * 180. / 3.14;
    cout << "Rotation angle (degrees): " << rot_angle_deg << endl;
    return rot_angle_deg;
}

// gets the rotation angle using hough transform (works best with a single big rectangle, like a mask)
double get_angle_hough(Mat& input) {
    Mat edge;
    Canny(input, edge, 50, 200, 3);
    vector<Vec2f> lines;
    int thresh = 0;
    do {
      thresh += 10;
      HoughLines(edge, lines, 5, CV_PI/180, thresh, 0, 0);
    } while (lines.size() > 10);
    double angles = 0.;
    int angle_count = 0;
    for (Vec2f line : lines) {
        double pang = principal_angle(line[1]); // (-pi, pi)
        if (pang < -CV_PI / 2) {
          pang += CV_PI;
        }
        if (pang > CV_PI / 2) {
          pang -= CV_PI;
        }
        // (-pi/2, pi/2)
        cout << line[1] << " " << pang  << endl;
        if (pang < CV_PI / 4 && pang > -CV_PI/4) {
          angles += pang;
          angle_count++;
        }
    }
    double rot_angle_rad = angles / double(angle_count);
    double rot_angle_deg = rot_angle_rad * 180. / CV_PI;
    cout << "Rotation angle (degrees): " << rot_angle_deg << endl;
    return rot_angle_deg;
}

// helper to rotate by an angle (in degrees)
void rotate(Mat& input, Mat& output, double angle) {
    int midr = input.rows / 2;
    int midc = input.cols / 2;
    // Actually rotate the input
    Mat rot = getRotationMatrix2D(Point(midc, midr), angle, 1.f);
    warpAffine(input, output, rot, input.size());
}

// various hough experiments to try to get the grid lines directly
void hough_playing_for_grid_outline(Mat& input, Mat& output) {
    Canny(input, input, 50, 200, 3);

    int v = 0;
    cvtColor(input, output, CV_GRAY2BGR);
    if (v == 0) {
      vector<Vec2f> lines;
      int thresh = 0;
      do {
        thresh += 10;
        HoughLines(input, lines, 5, CV_PI/180, thresh, 0, 0);
      } while (lines.size() > 10);
      for (size_t i = 0; i < lines.size(); ++i) {
          float rho = lines[i][0];
          float theta = lines[i][1];
          cout << "Angle: " << theta << endl;
          Point pt1, pt2;
          double a = cos(theta), b = sin(theta);
          double x0 = a*rho, y0 = b*rho;
          pt1.x = cvRound(x0 + 1000*(-b));
          pt1.y = cvRound(y0 + 1000*a);
          pt2.x = cvRound(x0 - 1000*(-b));
          pt2.y = cvRound(y0 - 1000*a);
          line(output, pt1, pt2, Scalar(0, 0, 255), 1, CV_AA);
      }
    } else if (v == 1) {
      vector<Vec4i> lines;
      int thresh = 0;
      do {
        cout << thresh << endl;
        thresh += 100;
        HoughLinesP(input, lines, 10, CV_PI/18, thresh, 50, 0);
      } while (lines.size() > 100);
      for (size_t i = 0; i < lines.size(); ++i) {
          Point pt1, pt2;
          cout << "a line"<< endl;
          pt1.x = lines[i][0];
          pt1.y = lines[i][1];
          pt2.x = lines[i][2];
          pt2.y = lines[i][3];
          line(output, pt1, pt2, Scalar(0, 0, 255), 1, CV_AA);
      }
    } else if (v == 2) {
      vector<Point2f> corners;
      goodFeaturesToTrack(input, corners, 4, 0.01, 100);
      for (size_t i = 0; i < corners.size(); ++i) {
        circle(output, corners[i], 4, Scalar(0, 0, 255));
      }
    }
}

// crossword mask
void get_cw_mask(Mat& input, Mat& outputMask) {
    threshold(input, input, 128., 255., THRESH_BINARY);
    // Fill from all corners
    int in = 1;
    Scalar col = Scalar(0,0,0);
    Mat filled = input.clone();
    Mat mask = Mat::zeros(filled.rows + 2, filled.cols + 2, CV_8UC1);
    floodFill(filled, mask, Point(in,in), col);
    floodFill(filled, mask, Point(filled.cols - in,in), col);
    floodFill(filled, mask, Point(filled.cols - in,filled.rows - in), col);
    floodFill(filled, mask, Point(in,filled.rows - in), col);
    // Find average white pixel
    long tr = 0;
    long tc = 0;
    vector<Point> locs;
    findNonZero(filled, locs);
    for (Point p : locs) { // C++11, nice!
      tc += p.x;
      tr += p.y;
    }
    cvtColor(input, input, CV_GRAY2BGR);
    Mat oldmask = mask.clone();
    Scalar bc = Scalar(255, 255, 255);
    floodFill(filled, mask, Point(double(tc) / double(locs.size()), double(tr) / double(locs.size())), Scalar(255, 0, 0), NULL, bc, bc);
    mask -= oldmask;
    outputMask = mask(Rect(1, 1, input.cols, input.rows));
    //floodFill(input, mask, Point(in, in), Scalar(0, 0, 255), NULL, bc, bc);
    ////circle(input, Point(double(tc) / double(locs.size()), double(tr) / double(locs.size())), 4, Scalar(0, 0, 255));

    outputMask.convertTo(outputMask, CV_8UC1, 255.);
    //output = input;
}

// orthogonal truncated crossword
void get_cw_orth_trunc(Mat& input, Mat& output) {
    Mat mask;
    get_cw_mask(input, mask);
    showit(mask);

    double angle = get_angle_hough(mask);
    rotate(mask, mask, angle);
    rotate(input, input, angle);
    showit(mask);
    vector<Point> whites;
    findNonZero(mask, whites);
    output = input(boundingRect(whites));
}

int main(int argc, char **argv) {
    if (argc != 2) {
        cout << "Usage: filter_test image" << endl;
        return -1;
    }

    Mat input;
    input = imread(argv[1], CV_LOAD_IMAGE_GRAYSCALE);

    if (!input.data) {
        cout << "Could not open or find the image" << endl;
        return -1;
    }

    Mat cw;
    namedWindow("cwfloat", WINDOW_AUTOSIZE);
    get_cw_orth_trunc(input, cw);
    showit(cw);

    return 0;
}
