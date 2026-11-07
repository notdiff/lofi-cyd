#pragma once
#include "generated/rabbit_assets.h"

inline const RabbitSprite& rabbitSpriteForPose(uint8_t page, const RabbitPose& pose) {
    if (pose.action == RabbitAction::Fall)
        return page == 1 ? kRabbitfalltool[pose.frame] : kRabbitfallnotebook[pose.frame];
    return page == 0 ? kRabbitsleeping[pose.frame]
                     : (page == 1 ? kRabbittool[pose.frame] : kRabbitnotebook[pose.frame]);
}

inline bool rabbitHit(uint8_t page, const RabbitPose& pose, int x, int y) {
    if (!rabbitEnabled(page) || pose.frame == 255)
        return false;
    const auto region = rabbitRegion(page);
    const int localX = x - region.x - rabbitLeft(page) - pose.shift;
    const int localY = y - region.y - rabbitTop(page) + pose.lift;
    const int width = rabbitWidth(page), height = rabbitHeight(page);
    if (localX < 0 || localY < 0 || localX >= width || localY >= height)
        return false;
    const auto& sprite = rabbitSpriteForPose(page, pose);
    uint8_t indexes[144];
    rabbitRow(sprite, localY * sprite.height / height, indexes);
    const uint8_t index = indexes[localX * sprite.width / width];
    return pgm_read_byte(sprite.opacity + index) >= 3;
}
