# Models

`face_detection_yunet_2023mar.onnx` is YuNet from the OpenCV Model Zoo
(https://github.com/opencv/opencv_zoo/tree/main/models/face_detection_yunet),
distributed under the MIT License. It detects faces and five landmarks
(both eyes, nose tip, both mouth corners), which is all ScreenSense needs
to estimate head direction.

SHA-256: `8f2383e4dd3cfbb4553ea8718107fc0423210dc964f9f4280604804ed2552fa4`

`face_detection_yunet_2022mar.onnx` is the earlier YuNet release from the same
place, under the same MIT License (Copyright (c) 2020 Shiqi Yu). OpenCV 4.8
changed the model format, so builds against OpenCV 4.5.4 to 4.7, such as on
Ubuntu 24.04 and Debian 12, install this one instead. It was taken from the
OpenCV Model Zoo at commit `c97242ce7f2a554e288b50eabd9f5df957e78801`, the
last version before its removal.

SHA-256: `50ef07f702a31741ca46a4c0d947773b64143b9362780237bf0d427d6c79bab7`
(matches the model zoo's Git LFS record)

`iris_landmark.onnx` is MediaPipe's iris landmark model
(https://storage.googleapis.com/mediapipe-assets/iris_landmark.tflite),
Copyright The MediaPipe Authors, distributed under the Apache License 2.0
(see `LICENSE.iris_landmark`). ScreenSense uses it to tell where the eyes are
pointing within the head.

It was changed from TensorFlow Lite to ONNX with `tflite2onnx`, because many
OpenCV packages cannot load TensorFlow Lite files. The weights are unchanged:
on a random input, every output of the two files agreed to within 0.00004
pixels.

SHA-256 of the original: `d1744d2a09c25f501d39eba4faff47e53ecca8852c5ce19bce8eeac39357521f`

SHA-256 of the ONNX file: `44b2816b19b993a9caf5eb294523bea9ccea81bc0a632e4d7d9123b0aaf718b0`
