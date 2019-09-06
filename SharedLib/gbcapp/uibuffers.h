#pragma once

const int mainMenuFloatCount = 60;
float mainMenuUiFloats[] = {
        -1.0f, 1.0f, 0.0f, 0.0f, 0.0f,
        -1.0f, 0.5f, 0.0f, 0.0f, 0.25f,
        1.0f,  0.5f, 0.0f, 1.0f, 0.25f,
        1.0f,  0.5f, 0.0f, 1.0f, 0.25f,
        1.0f,  1.0f, 0.0f, 1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f, 0.0f, 0.0f,

        -1.0f, -0.5f, 0.0f, 0.0f, 0.25f,
        -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
        1.0f,  -1.0f, 0.0f, 1.0f, 0.0f,
        1.0f,  -1.0f, 0.0f, 1.0f, 0.0f,
        1.0f,  -0.5f, 0.0f, 1.0f, 0.25f,
        -1.0f, -0.5f, 0.0f, 0.0f, 0.25f
};

const int windowFloatCount = 30;
float windowFloats[] = {
        -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,
        1.0f, -1.0f, 0.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 0.0f, 1.0f, 0.0f,
        1.0f, 1.0f, 0.0f, 1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f, 0.0f, 0.0f,
        -1.0f, -1.0f, 0.0f, 0.0f, 1.0f
};

float* generateWindowFloats(const float fillAmount, const float aspect) {
    auto const newFloats = new float[30];
    for (size_t i = 0; i < 30; i++) {
        newFloats[i] = windowFloats[i];
    }

    const float scaleFactor = aspect < 1.0f ? fillAmount * aspect : fillAmount;
    for (size_t i = 0; i < 6; i++) {
        newFloats[i * 5] *= scaleFactor;
        newFloats[i * 5 + 1] *= scaleFactor;
    }
    return newFloats;
}
