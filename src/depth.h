#pragma once

#include "pipeline.h"
#include "viewport.h"
#include <string>
#include <vector>

// Run MiDaS depth estimation on an image, returns a depth map (float per pixel, higher = closer)
std::vector<float> estimate_depth(const ImageData& img, const std::string& model_path, LogCallback log);

// Convert depth map + image to a 3D mesh via point cloud back-projection + simple meshing
Mesh depth_to_mesh(const ImageData& img, const std::vector<float>& depth, int downsample, LogCallback log);
