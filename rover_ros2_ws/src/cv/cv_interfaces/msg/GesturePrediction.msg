# raw classifier output published by classifier_node
# subscribed by filter_node which decides if gesture really happened

std_msgs/Header header    # same stamp as the Keypoints message it came from

bool person_detected      # false -> no usable person, probabilities aren't meaningful
string[] class_names
float32[] probabilities   # softmax output

string top_class          # class w/ highest probability
float32 top_confidence    # probability