#ifndef LABEL_H_
#define LABEL_H_

#include <string>
#include <array>

//Digitanie COS11 labels

struct Label {
    unsigned char value;
    std::string label;
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    Label(int value, std::string label, unsigned char red, unsigned char green, unsigned char blue): value(value), label(label), red(red), green(green), blue(blue) {}
};

static std::array<Label, 6> LABELS {
    Label(0, "other", 255, 255, 255),
    Label(1, "low vegetation", 0, 250, 50),
    Label(2, "railways", 200, 100, 200),
    Label(3, "high vegetation", 0, 100, 50),
    Label(4, "building", 250, 50, 50),
    Label(5, "road", 100, 100, 100)
};

#endif  /* !LABEL_H_ */
