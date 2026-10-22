#include "autoneg/adapter/opencv/opencv_vision.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <numbers>
#include <vector>

namespace autoneg::adapter::opencv {

namespace {

/// Einkanaliges float-Bild (bei drei Kanälen Luminanz Rec. 2020); Kopie.
base::Expected<cv::Mat> toMat(const image::ImageBuffer& img)
{
    const auto& s = img.spec();
    if (img.empty() || s.format != image::PixelFormat::F32 || img.host().empty()) {
        return base::makeError(base::ErrorCode::InvalidArgument, "IVision erwartet F32 im Hauptspeicher");
    }
    cv::Mat m(s.height, s.width, CV_32F);
    const auto v = img.host();
    const auto ch = static_cast<std::size_t>(s.channels);
    for (int y = 0; y < s.height; ++y) {
        auto* row = m.ptr<float>(y);
        for (int x = 0; x < s.width; ++x) {
            const std::size_t i =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(s.width) + static_cast<std::size_t>(x);
            row[x] = ch == 1 ? v[i] : 0.2627F * v[i * ch] + 0.6780F * v[i * ch + 1] + 0.0593F * v[i * ch + 2];
        }
    }
    return m;
}

/// Maske 0/1 als 8-bit 0/255.
base::Expected<cv::Mat> toMask(const image::ImageBuffer& img)
{
    auto m = toMat(img);
    if (!m) {
        return m;
    }
    cv::Mat out;
    cv::threshold(*m, out, 0.5, 255.0, cv::THRESH_BINARY);
    out.convertTo(out, CV_8U);
    return out;
}

image::ImageBuffer fromMask(const cv::Mat& m)
{
    std::vector<float> v(static_cast<std::size_t>(m.rows) * static_cast<std::size_t>(m.cols));
    for (int y = 0; y < m.rows; ++y) {
        const auto* row = m.ptr<std::uint8_t>(y);
        for (int x = 0; x < m.cols; ++x) {
            v[static_cast<std::size_t>(y) * static_cast<std::size_t>(m.cols) + static_cast<std::size_t>(x)] =
                row[x] != 0 ? 1.0F : 0.0F;
        }
    }
    return image::ImageBuffer::fromFloats(
               image::ImageSpec{m.cols, m.rows, 1, image::PixelFormat::F32, image::ColorSpace::WorkingLinear},
               v)
        .value();
}

template <typename F>
auto guarded(F&& f) -> decltype(f())
{
    try {
        return f();
    } catch (const cv::Exception& e) {
        return base::makeError(base::ErrorCode::Internal, std::string("OpenCV: ") + e.what());
    }
}

} // namespace

base::Expected<image::ImageBuffer> OpenCvVision::edges(const image::ImageBuffer& img, float sigma)
{
    return guarded([&]() -> base::Expected<image::ImageBuffer> {
        auto m = toMat(img);
        if (!m) {
            return std::unexpected(m.error());
        }
        cv::Mat smooth;
        if (sigma > 0.0F) {
            cv::GaussianBlur(*m, smooth, cv::Size(0, 0), sigma, sigma, cv::BORDER_REPLICATE);
        } else {
            smooth = *m;
        }
        cv::Mat dx;
        cv::Mat dy;
        cv::Sobel(smooth, dx, CV_32F, 1, 0, 3, 1.0, 0.0, cv::BORDER_REPLICATE);
        cv::Sobel(smooth, dy, CV_32F, 0, 1, 3, 1.0, 0.0, cv::BORDER_REPLICATE);
        cv::Mat mag;
        cv::magnitude(dx, dy, mag);
        double maxMag = 0.0;
        cv::minMaxLoc(mag, nullptr, &maxMag);
        if (maxMag <= 0.0) {
            return fromMask(cv::Mat::zeros(m->rows, m->cols, CV_8U));
        }
        // Canny auf 16-bit-Gradienten; Skalierung so, dass der größte Betrag 16000 ist
        const double scale = 16000.0 / maxMag;
        cv::Mat dx16;
        cv::Mat dy16;
        dx.convertTo(dx16, CV_16S, scale);
        dy.convertTo(dy16, CV_16S, scale);
        cv::Mat out;
        cv::Canny(dx16, dy16, out, 0.1 * 16000.0, 0.2 * 16000.0, true);
        return fromMask(out);
    });
}

base::Expected<std::vector<ports::Polygon>> OpenCvVision::contours(const image::ImageBuffer& mask)
{
    return guarded([&]() -> base::Expected<std::vector<ports::Polygon>> {
        auto m = toMask(mask);
        if (!m) {
            return std::unexpected(m.error());
        }
        std::vector<std::vector<cv::Point>> found;
        cv::findContours(*m, found, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        std::vector<ports::Polygon> out;
        for (const auto& c : found) {
            ports::Polygon p;
            for (const cv::Point& pt : c) {
                p.points.push_back({static_cast<double>(pt.x), static_cast<double>(pt.y)});
            }
            out.push_back(std::move(p));
        }
        return out;
    });
}

base::Expected<std::vector<ports::Line>> OpenCvVision::lines(const image::ImageBuffer& edges)
{
    return guarded([&]() -> base::Expected<std::vector<ports::Line>> {
        auto m = toMask(edges);
        if (!m) {
            return std::unexpected(m.error());
        }
        std::vector<cv::Vec4i> found;
        cv::HoughLinesP(*m, found, 1.0, std::numbers::pi / 720.0, 20, 20.0, 3.0);
        std::vector<ports::Line> out;
        for (const cv::Vec4i& l : found) {
            out.push_back({{static_cast<double>(l[0]), static_cast<double>(l[1])},
                           {static_cast<double>(l[2]), static_cast<double>(l[3])}});
        }
        return out;
    });
}

base::Expected<image::ImageBuffer> OpenCvVision::morphology(const image::ImageBuffer& mask, ports::MorphOp op,
                                                            int radius)
{
    return guarded([&]() -> base::Expected<image::ImageBuffer> {
        auto m = toMask(mask);
        if (!m) {
            return std::unexpected(m.error());
        }
        const int r = std::max(radius, 0);
        const cv::Mat element = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(2 * r + 1, 2 * r + 1));
        int cvOp = cv::MORPH_OPEN;
        switch (op) {
        case ports::MorphOp::Erode:
            cvOp = cv::MORPH_ERODE;
            break;
        case ports::MorphOp::Dilate:
            cvOp = cv::MORPH_DILATE;
            break;
        case ports::MorphOp::Open:
            cvOp = cv::MORPH_OPEN;
            break;
        case ports::MorphOp::Close:
            cvOp = cv::MORPH_CLOSE;
            break;
        }
        cv::Mat out;
        // Rand beeinflusst das Ergebnis nicht (OpenCV-Standard morphologyDefaultBorderValue)
        cv::morphologyEx(*m, out, cvOp, element);
        return fromMask(out);
    });
}

} // namespace autoneg::adapter::opencv
