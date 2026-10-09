# Vulkan Text Rendering Debug Guide

## Problem
- Server list: Pink logo, invisible text
- Loading screen: Works fine

## What We Know
1. Text is being rendered with BLACK color (0,0,0,1) - see logs
2. Logo appears PINK (wrong texture sampling)
3. Loading screen renders correctly

## Most Likely Cause
The issue is that black text is actually being SET by the UI code, not a rendering bug. But why does it work in OpenGL?

## Next Steps to Debug

1. Compare what color the UI INTENDS to set:
   - Check client code that renders server list
   - Look for where text color is specified

2. Check if there's a coordinate system mismatch:
   - Vulkan Y-axis might be interpreted differently
   - Font atlas coordinates might be wrong

## Quick Test
Try forcing white text globally by modifying VulkanRenderer::SetColorAlphaPremultiplied:
```cpp
void VulkanRenderer::SetColorAlphaPremultiplied(Vector4 color) {
    // HACK: Force white for testing
    if (color.x < 0.1 && color.y < 0.1 && color.z < 0.1) {
        SPLog("Forcing black->white: was (%.2f,%.2f,%.2f,%.2f)",
              color.x, color.y, color.z, color.w);
        color = Vector4(1,1,1,1);
    }
    drawColorAlphaPremultiplied = color;
}
```

This will show if the problem is just black text being set.
