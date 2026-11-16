# Positions are confirmed waypoints, or UNKNOWN/MOVING. Never inferred from time.
string phase
string platform_position
string npk_position
bool soil_collected
bool bradford_retracted
bool ready_to_drive
bool fault
# Zero-based index; -1 if carousel position is unknown.
int32 cuvette_index
string detail
