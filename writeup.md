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
car position are added to it, also 3 equally spaced points that conatins the planned next lane are added to the path.
The points are after that shifted to the car referance and added to the spline function.
The next path points are finally deduced from the spline output after converting back to the first coordinates.


### 2. Car states

Three states for the car are defined:
Keep lane, Change right and change left.
To change lane, other cars must be in the car lane, the car must have the right speed
and no cars are blocking the other lanes.

### 3. Speed and distance control
To control the speed of the car a PD controller is tuned to change smoothly the speed.
Another PD controller is also tuned for keeping a desired distance to the closest car in the car lane. 


