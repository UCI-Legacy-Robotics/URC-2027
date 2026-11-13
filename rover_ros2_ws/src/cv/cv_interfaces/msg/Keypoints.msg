std_msgs/Header header        # stamp = time the source image was captured

bool detected
int32 track_id                # -1 if not tracking
float32 person_confidence

uint32 image_width
uint32 image_height

float32[17] x                 # pixels (or normalized: pick one and document later)
float32[17] y
float32[17] confidence