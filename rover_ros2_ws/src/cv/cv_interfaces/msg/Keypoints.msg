std_msgs/Header header        # stamp = time the source image was captured

bool detected
float32 person_confidence

float32[17] x                 # pixels (or normalized: pick one and document later)
float32[17] y
float32[17] confidence