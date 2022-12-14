#undef EPS // specreg.h defines EPS which interfere with opencv
#include "opencv2/core.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/imgcodecs.hpp"
#define EPS 192

#include <esp_log.h>
#include <esp_err.h>
#include <esp_timer.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_freertos_hooks.h>
#include <iostream>
#include <cmath>
#include <map>

#include "system.h"
#include "app_screen.h"
#include "app_camera.h"

#include "iot_lvgl.h"

using namespace cv;
using namespace std;

extern "C"
{
    void app_main(void);
}

#define TAG "main"

extern CEspLcd *tft;

static lv_obj_t *lvCameraImage; // Camera image object

void gui_boot_screen()
{
    static lv_style_t style;
    lv_style_init(&style);

    lv_style_set_radius(&style, LV_STATE_DEFAULT, 2);
    lv_style_set_bg_opa(&style, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_style_set_bg_color(&style, LV_STATE_DEFAULT, LV_COLOR_MAKE(190, 190, 190));
    lv_style_set_border_width(&style, LV_STATE_DEFAULT, 2);
    lv_style_set_border_color(&style, LV_STATE_DEFAULT, LV_COLOR_MAKE(142, 142, 142));

    lv_style_set_pad_top(&style, LV_STATE_DEFAULT, 60);
    lv_style_set_pad_bottom(&style, LV_STATE_DEFAULT, 60);
    lv_style_set_pad_left(&style, LV_STATE_DEFAULT, 60);
    lv_style_set_pad_right(&style, LV_STATE_DEFAULT, 60);

    lv_style_set_text_color(&style, LV_STATE_DEFAULT, LV_COLOR_MAKE(102, 102, 102));
    lv_style_set_text_letter_space(&style, LV_STATE_DEFAULT, 5);
    lv_style_set_text_line_space(&style, LV_STATE_DEFAULT, 20);

    /*Create an object with the new style*/
    lv_obj_t *obj = lv_label_create(lv_scr_act(), NULL);
    lv_obj_add_style(obj, LV_LABEL_PART_MAIN, &style);
    lv_label_set_text(obj, "TTGO\n"
                           "demo!");
    lv_obj_align(obj, NULL, LV_ALIGN_CENTER, 0, 0);
    wait_msec(3000);
}

// Function to calculate distance
float distance(int x1, int y1, int x2, int y2)
{
    // Calculating distance
    return sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2) * 1.0);
}

void gui_init()
{
    // Create screen
    lv_obj_t *scr = lv_obj_create(nullptr, nullptr);
    lv_scr_load(scr);
}

esp_err_t updateCameraImage(const cv::Mat &img)
{
    // static variables because they must still be available when lv_task_handler() is called
    static Mat imgCopy;
    static lv_img_dsc_t my_img_dsc;

    if (img.empty())
    {
        ESP_LOGW(TAG, "Can't display empty image");
        return ESP_ERR_INVALID_ARG;
    }

    // convert image to bgr565 if needed
    if (img.type() == CV_8UC1)
    { // grayscale image
        cvtColor(img, imgCopy, COLOR_GRAY2BGR565, 1);
    }
    else if (img.type() == CV_8UC3)
    { // BGR888 image
        cvtColor(img, imgCopy, COLOR_BGR2BGR565, 1);
    }
    else if (img.type() == CV_8UC2)
    { // BGR565 image
        img.copyTo(imgCopy);
    }

    my_img_dsc.header.always_zero = 0;
    my_img_dsc.header.w = imgCopy.cols;
    my_img_dsc.header.h = imgCopy.rows;
    my_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    my_img_dsc.data_size = imgCopy.size().width * imgCopy.size().height;
    my_img_dsc.data = imgCopy.ptr<uchar>(0);

    lv_img_set_src(lvCameraImage, &my_img_dsc); /* Set the created file as image */
    lv_obj_set_pos(lvCameraImage, -40, 0);

    return ESP_OK;
}

// mode of the demo
enum class DisplayMode : uint8_t
{
    RGB,
    GRAYSCALE,
    BINARIZED,
    EDGES,
    NUM_OF_MODES
};
static DisplayMode currentDisplayMode;

static const std::string displayModeToString(DisplayMode dispMode)
{
    const std::map<DisplayMode, const std::string> DisplayModeStrings{
        {DisplayMode::RGB, "RGB"},
        {DisplayMode::GRAYSCALE, "GRAYSCALE"},
        {DisplayMode::BINARIZED, "BINARIZED"},
        {DisplayMode::EDGES, "EDGES"},
    };
    auto it = DisplayModeStrings.find(dispMode);
    return (it == DisplayModeStrings.end()) ? "Out of range" : it->second;
}

/**
 * Task doing the demo: Getting image from camera, processing it with opencv depending on the displayMode and
 * displaying it on the lcd
 */
void demo_task(void *arg)
{
    static Mat src;
    ESP_LOGI(TAG, "Starting demo_task");

    // Display memory infos
    disp_infos();

    // tft->setRotation(2); // rotation needed if camera is on the back of the device
    sensor_t *s = esp_camera_sensor_get();

    // Init camera image Lvgl object
    // lvCameraImage = lv_img_create(lv_disp_get_scr_act(nullptr), nullptr);
    // lv_obj_move_foreground(lvCameraImage);
    gpio_pad_select_gpio(GPIO_NUM_4);                 // ????????????GPIO
    gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT); // ?????????GPIO????????????

    while (true)
    {
        currentDisplayMode = DisplayMode::EDGES;
        // auto start = esp_timer_get_time();
        // gpio_set_level(GPIO_NUM_4, 1); // ?????????GPIO???????????????
        camera_fb_t *fb = esp_camera_fb_get();
        // gpio_set_level(GPIO_NUM_4, 0); // ?????????GPIO???????????????
        // cout << "theta1=" << to_string(1) << ","
        //      << "theta2=" << to_string(51) << endl;
        // cout << "AT+SEND=1,"
        //      << "theta2=" << to_string(51) << endl;
        // int value1 = 1, value2 = 51;
        // int valuelen = to_string(value1 + 100).length() + to_string(value2 + 100).length() + 1;
        // String mymessage;
        // mymessage = mymessage + "AT+SEND=1" + "," + to_string(valuelen) + "," + to_string(value1 + 100) + "," + to_string(value2 + 100);
        // cout << mymessage << endl;
        // wait_msec(500);
        if (!fb)
        {
            // ESP_LOGE(TAG, "Camera capture failed");
        }
        else
        {
            if (s->pixformat == PIXFORMAT_JPEG)
            {
                // TFT_jpg_image(CENTER, CENTER, 0, -1, NULL, fb->buf, fb->len);
                esp_camera_fb_return(fb);
                fb = NULL;
            }
            else
            {                                                            // RGB565 pixformat
                Mat inputImage(fb->height, fb->width, CV_8UC2, fb->buf); // rgb565 is 2 channels of 8-bit unsigned

                if (currentDisplayMode == DisplayMode::RGB)
                {
                }
                else if (currentDisplayMode == DisplayMode::GRAYSCALE)
                {
                    cvtColor(inputImage, inputImage, COLOR_BGR5652GRAY);
                }
                else if (currentDisplayMode == DisplayMode::BINARIZED)
                {
                    cvtColor(inputImage, inputImage, COLOR_BGR5652GRAY);
                    threshold(inputImage, inputImage, 128, 255, THRESH_BINARY);
                }
                else if (currentDisplayMode == DisplayMode::EDGES)
                {
                    cvtColor(inputImage, inputImage, COLOR_BGR5652GRAY);
                    // cout << "original pic" << inputImage << endl;
                    inputImage.copyTo(src);
                    // Reduce noise with a kernel 3x3
                    GaussianBlur(inputImage, inputImage, Size(3, 3), 0);
                    /** Apply the canny edges detector with:
                     * - low threshold = 50
                     * - high threshold = 4x low
                     * - sobel kernel size = 3x3
                     */
                    int lowThresh = 50;
                    int kernSize = 3;
                    Canny(inputImage, inputImage, lowThresh, 3 * lowThresh, kernSize);
                    // cv::resize(inputImage, resizeImg, cv::Size(40, 30), cv::INTER_LINEAR);
                    // cout << "original pic" << inputImage << endl;
                    // ESP_LOGE(TAG, "DetectCanny", inputImage);
                    // ???????????????
                    vector<Vec3f> pcircles;
                    HoughCircles(inputImage, pcircles, HOUGH_GRADIENT, 1, 30, lowThresh, 30, 30, 50);
                    if (pcircles.size() == 1)
                    {

                        // for (size_t i = 0; i < pcircles.size(); i++)
                        // {
                        Vec3f cc = pcircles[0];
                        circle(src, Point(cc[0], cc[1]), cc[2], Scalar(0, 0, 255), 2, LINE_AA);
                        circle(src, Point(cc[0], cc[1]), 2, Scalar(125, 25, 255), 2, LINE_AA);
                        // }
                        // cout << "src=" << src << endl;
                        // ESP_LOGE(TAG, "Detect only one circle: %d", pcircles.size());
                        vector<Vec4i> lines;
                        // HoughLines(src1, lines, 1, CV_PI / 180, 150, 0, 0);
                        HoughLinesP(inputImage, lines, 1, CV_PI / 180, 3, 10, 1); // ?????????????????????
                        // ESP_LOGE(TAG, "Detect lines: %d", lines.size());
                        for (size_t i = 0; i < lines.size(); i++)
                        {
                            Vec4i l = lines[i];
                            // [x0, y0, x1, y1]
                            // line(src, Point(l[0], l[1]), Point(l[2], l[3]), Scalar(255, 0, 0), 1, LINE_AA);
                            if ((distance(l[0], l[1], cc[0], cc[1]) < cc[2]) & (distance(l[2], l[3], cc[0], cc[1]) < cc[2])) // ????????????
                            {
                                if ((min(l[0], l[2]) - 5 <= cc[0]) & (cc[0] <= max(l[0], l[2]) + 5) & (min(l[1], l[3]) - 5 <= cc[1]) & (cc[1] <= max(l[1], l[3]) + 5)) // ????????????????????????
                                {
                                    float true_theta = 0;
                                    float delta_x = abs(l[0] - cc[0]);
                                    float delta_y = abs(l[1] - cc[1]);
                                    if (((l[0] - cc[0]) < 0) & ((l[1] - cc[1]) > 0))
                                    {
                                        true_theta = CV_PI / 2 - atan(delta_y / delta_x);
                                    }
                                    else if (((l[0] - cc[0]) < 0) & ((l[1] - cc[1]) < 0))
                                    {
                                        true_theta = CV_PI / 2 + atan(delta_y / delta_x);
                                    }
                                    else if (((l[0] - cc[0]) > 0) & ((l[1] - cc[1]) < 0))
                                    {
                                        true_theta = 3 * CV_PI / 2 - atan(delta_y / delta_x);
                                    }
                                    else if (((l[0] - cc[0]) > 0) & ((l[1] - cc[1]) > 0))
                                    {
                                        true_theta = 3 * CV_PI / 2 + atan(delta_y / delta_x);
                                    }
                                    line(src, Point(l[0], l[1]), Point(l[2], l[3]), Scalar(255, 0, 0), 1, LINE_AA);

                                    // cout << "src=" << src << endl;
                                    // ESP_LOGE(TAG, "True theta: %.4f", true_theta / CV_PI * 180);
                                    // ESP_LOGE(TAG, "line length: %.4f", distance(l[0], l[1], l[2], l[3]));
                                    int theta1 = (int)(true_theta / CV_PI * 180);
                                    int theta2 = (theta1 + 180) % 360;
                                    float range1 = 0.1818, range2 = 0.4285;
                                    int value1, value2;
                                    if (theta1 <= 110)
                                    {
                                        value1 = (int)(theta1 * range1);
                                    }
                                    else if (theta1 < 250)
                                    {
                                        value1 = (int)(20 + (theta1 - 110) * range2);
                                    }
                                    else if (theta1 >= 250)
                                    {
                                        value1 = (int)(80 + (theta1 - 250) * range1);
                                    }

                                    if (theta2 <= 110)
                                    {
                                        value2 = (int)(theta2 * range1);
                                    }
                                    else if (theta2 < 250)
                                    {
                                        value2 = (int)(20 + (theta2 - 110) * range2);
                                    }
                                    else if (theta2 >= 250)
                                    {
                                        value2 = (int)(80 + (theta2 - 250) * range1);
                                    }

                                    int valuelen = to_string(value1 + 100).length() + to_string(value2 + 100).length() + 1;
                                    String mymessage;
                                    mymessage = mymessage + "AT+SEND=1" + "," + to_string(valuelen) + "," + to_string(value1 + 100) + "," + to_string(value2 + 100);
                                    cout << mymessage << endl;
                                    wait_msec(100);
                                }
                            }
                        }
                        // cout << "src=" << src << endl;
                    }
                    else
                    {
                        // ESP_LOGE(TAG, "Detect too many circles or zero: %d", pcircles.size());
                    }
                }
                else
                {
                    // ESP_LOGE(TAG, "Wrong display mode: %d", (int)currentDisplayMode);
                }

                // display image on lcd
                // updateCameraImage(inputImage);
            }
        }
        wait_msec(1000);
        // ESP_LOGI(TAG, "%s mode: around %f fps", displayModeToString(currentDisplayMode).c_str(), 1.0f / ((esp_timer_get_time() - start) / 1000000.0f));
    }
}

/**
 * Task changing the current displayMode at regular interval
 */
// void timer_task(void *arg)
// {
//     while (true)
//     {
//         wait_msec(10000);
//         currentDisplayMode = static_cast<DisplayMode>((static_cast<int>(currentDisplayMode) + 1) % static_cast<int>(DisplayMode::NUM_OF_MODES));
//     }
// }

void app_main()
{
    ESP_LOGI(TAG, "Starting main");

    /* initializations */
    app_camera_init();
    // lvgl_init();
    // gui_init();

    /* display boot screen */
    // gui_boot_screen();

    /* Display memory infos */
    disp_infos();

    /* Define Lora ID & address */
    cout << "AT+NETWORKID=10" << endl;
    wait_msec(500);
    cout << "AT+ADDRESS=4" << endl;
    wait_msec(500);
    // ESP_LOGI(TAG, "Display width = %d, height = %d", tft->width(), tft->height());

    /* Start the tasks */
    xTaskCreatePinnedToCore(demo_task, "demo", 1024 * 9, nullptr, 24, nullptr, 0);
    // xTaskCreatePinnedToCore(timer_task, "timer", 1024 * 1, nullptr, 24, nullptr, 0);
}