# **CarND-Path Planning-Project** 

## Writeup
---

**Path Planning Project**
* This repository conatins the implementation of path planner for a self driving car simulation in c++.
* This project is part Udacity self driving car nanodegree.

### 1. Generating paths
To generate the paths a spline is used to guarantee a smooth path change.
For this purpose, the library spline.h is included.
Using the previous path points, two tangent points accounting for the next 
car position are added to it, also 4 equally spaced points that conatins the planned next lane are added to the path.
The points are after that shifted to the car referance and added to the spline function.
The next path points are finally deduced from the spline output after converting back to the first coordinates.

### 2. Car states
Three states for the car are defined:
Keep lane, Change right and change left.

#### Keep lane: The car keeps its lane in the following cases:
  - No car exists in its lane, the car keeps accelerating to reach the limit speed.
  - A car exists in its lane but no other lane is free for change( Cars exists in the   other lanes 45m or less in front of the car or other cars are approching in the other lanes from behind the car 10m away or less). 
  In this case the car keeps its lane waiting for a possible lane change and tries to keep a security distance of 42m to the closest car in its lane.

####  Lane change to the right or the left: 
when a car is in the car lane and a lane change is possible to one of the other lanes(no car in front in a 40m distance and no car from bhind in 10m distance) the car starts the lane change. 
To insure a smooth lane change it starts only if the car has a speed higher than 34 miles per hour, the spacing between the path points is also adapted for the same purpose.

### 3. Speed and distance control
To control the speed of the car a PD controller is tuned to change smoothly the speed.
Another PD controller is also tuned for keeping a desired distance to the closest car in the car lane. 


