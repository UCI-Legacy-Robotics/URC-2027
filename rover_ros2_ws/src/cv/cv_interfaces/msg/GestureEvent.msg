# one confirmed gesture, published by filter_node
# only sent once per gesture (not every frame), this is what the auto controller listens to

std_msgs/Header header    # stamp of the prediction that confirmed the gesture

string gesture            # class name, e.g. "fetch"
float32 confidence        # smoothed probability when it fired
float32 held_sec          # how long it stayed above threshold before firing
