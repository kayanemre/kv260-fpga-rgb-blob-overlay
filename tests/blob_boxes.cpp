#include "../ps/color_boxes.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
constexpr unsigned width = 640, height = 480;
using RGB = std::array<uint8_t, 3>;
using Image = std::vector<uint8_t>;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Image solid(RGB color) {
    Image image(width * height * 3);
    for (unsigned i = 0; i < width * height; ++i)
        std::copy(color.begin(), color.end(), image.begin() + i * 3);
    return image;
}

void rectangle(Image& image, unsigned x, unsigned y, unsigned w, unsigned h, RGB color) {
    require(x + w <= width && y + h <= height, "test rectangle outside image");
    for (unsigned yy = y; yy < y + h; ++yy)
        for (unsigned xx = x; xx < x + w; ++xx)
            std::copy(color.begin(), color.end(), image.begin() + (yy * width + xx) * 3);
}

// Independent pixel reference for the PL contract: max absolute RGB-channel
// difference from the immediate left/above pixel, not a class or palette label.
std::vector<uint32_t> edge_map(const Image& image) {
    std::vector<uint32_t> edges(width * height);
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const unsigned i = y * width + x;
            unsigned left = x == 0 ? 255 : 0;
            unsigned up = y == 0 ? 255 : 0;
            for (unsigned channel = 0; channel < 3; ++channel) {
                int pixel = image[i * 3 + channel];
                if (x > 0)
                    left = std::max(left, unsigned(std::abs(pixel - image[(i - 1) * 3 + channel])));
                if (y > 0)
                    up = std::max(up, unsigned(std::abs(pixel - image[(i - width) * 3 + channel])));
            }
            edges[i] = left | (up << 8);
        }
    }
    return edges;
}

std::vector<BlobBox> detect(BlobBoxes& detector, const Image& image) {
    const auto edges = edge_map(image);
    return detector.detect(image, edges.data());
}

void expect_box(const std::vector<BlobBox>& boxes, unsigned x, unsigned y,
                unsigned w, unsigned h, RGB color) {
    auto match = std::find_if(boxes.begin(), boxes.end(), [&](const BlobBox& box) {
        return box.x0 == x && box.y0 == y && box.x1 == x + w - 1 && box.y1 == y + h - 1;
    });
    require(match != boxes.end(), "missing exact box at " + std::to_string(x) + "," + std::to_string(y));
    require(match->pixels == w * h, "wrong blob pixel count");
    require(match->mean == color, "wrong measured blob mean color");
}

void test_arbitrary_colors() {
    BlobBoxes detector;
    auto image = solid({235, 237, 239});
    // Includes two colors outside the old named palette, achromatic gray,
    // and true black: none may be silently treated as an unclassified pixel.
    const RGB colors[] = {{{15, 185, 193}}, {{137, 33, 181}}, {{93, 93, 93}}, {{0, 0, 0}}};
    for (unsigned i = 0; i < 4; ++i) rectangle(image, 40 + i * 160, 80, 64, 80, colors[i]);
    const auto boxes = detect(detector, image);
    require(boxes.size() == 4, "cyan/purple/gray/black scene must contain four blobs");
    for (unsigned i = 0; i < 4; ++i) expect_box(boxes, 40 + i * 160, 80, 64, 80, colors[i]);
}

void test_separate_same_color_islands() {
    BlobBoxes detector;
    auto image = solid({230, 230, 230});
    const RGB teal = {27, 159, 151};
    rectangle(image, 80, 200, 64, 48, teal);
    rectangle(image, 400, 200, 64, 48, teal);
    const auto boxes = detect(detector, image);
    require(boxes.size() == 2, "separate same-color islands must not share one box");
    expect_box(boxes, 80, 200, 64, 48, teal);
    expect_box(boxes, 400, 200, 64, 48, teal);
}

void test_dissimilar_adjacent_regions() {
    BlobBoxes detector;
    auto image = solid({230, 230, 230});
    const RGB ochre = {190, 100, 30}, olive = {50, 105, 20};
    rectangle(image, 96, 160, 64, 80, ochre);
    rectangle(image, 160, 160, 64, 80, olive);
    const auto boxes = detect(detector, image);
    require(boxes.size() == 2, "touching dissimilar regions must stay separate");
    expect_box(boxes, 96, 160, 64, 80, ochre);
    expect_box(boxes, 160, 160, 64, 80, olive);
}

void test_uniform_background_excluded() {
    BlobBoxes detector;
    for (RGB color : {RGB{0, 0, 0}, RGB{125, 125, 125}, RGB{255, 255, 255}, RGB{62, 193, 157}})
        require(detect(detector, solid(color)).empty(), "uniform full-frame background produced a box");
}

void test_overlay_preserves_actual_image() {
    BlobBoxes detector;
    auto image = solid({219, 225, 231});
    rectangle(image, 80, 80, 96, 80, {127, 40, 163});
    rectangle(image, 320, 240, 80, 80, {0, 0, 0});
    // A slightly different central patch must remain visible, not become a mask.
    rectangle(image, 112, 104, 16, 16, {130, 43, 166});
    const auto original = image;
    const auto boxes = detect(detector, image);
    require(boxes.size() == 2, "overlay fixture must contain two blobs");
    BlobBoxes::draw(image, boxes);
    unsigned changed = 0;
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            bool on_border = false;
            for (const auto& box : boxes)
                if (x >= box.x0 && x <= box.x1 && y >= box.y0 && y <= box.y1)
                    on_border |= x - box.x0 < 3 || box.x1 - x < 3 || y - box.y0 < 3 || box.y1 - y < 3;
            for (unsigned channel = 0; channel < 3; ++channel) {
                unsigned i = (y * width + x) * 3 + channel;
                if (image[i] != original[i]) {
                    ++changed;
                    require(on_border, "overlay changed an interior/background pixel");
                }
            }
        }
    }
    require(changed > 0, "overlay drew no visible box pixels");
    const unsigned black_border = (241 * width + 321) * 3;
    require(image[black_border + 1] > 0 || image[black_border + 2] > 0,
            "black blob border needs visible contrast");
}

void test_primary_colors_only() {
    BlobBoxes detector;
    auto image = solid({225, 225, 225});
    const RGB colors[] = {{{185,35,30}}, {{35,175,45}}, {{30,45,180}},
                          {{190,120,30}}, {{145,45,170}}, {{90,90,90}}};
    for(unsigned i=0;i<6;++i)rectangle(image,24+i*100,180,64,64,colors[i]);
    const auto edges=edge_map(image);
    const auto boxes=detector.detect(image,edges.data(),200,52,36,true);
    require(boxes.size()==3,"primary-only mode must retain only red, green, and blue blobs");
    expect_box(boxes,24,180,64,64,colors[0]);
    expect_box(boxes,124,180,64,64,colors[1]);
    expect_box(boxes,224,180,64,64,colors[2]);
    require(std::all_of(boxes.begin(),boxes.end(),[](const BlobBox& b){return b.primary>=0;}),
            "primary-only box is missing its RGB class");
}
}

int main() {
    try {
        test_arbitrary_colors();
        test_separate_same_color_islands();
        test_dissimilar_adjacent_regions();
        test_uniform_background_excluded();
        test_overlay_preserves_actual_image();
        test_primary_colors_only();
        std::cout << "PASS: 6 blob scenarios including RGB-only filtering and original-image overlay\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
