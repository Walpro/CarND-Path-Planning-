#include <fstream>
#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "json.hpp"
#include "spline.h"

using namespace std;

// for convenience
using json = nlohmann::json;

#define MAX_SP  49
using PATH_T = enum {KEEP_LANE,CHANGE_R, CHANGE_L };

struct CAR_STATE_T{
	PATH_T path;
	int lane;
	int des_lane;
	int spx;
	double speed;
	int sensors[3];
	double min_dst;
};

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.find_first_of("}");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

double distance(double x1, double y1, double x2, double y2)
{
	return sqrt((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
}
int ClosestWaypoint(double x, double y, const vector<double> &maps_x, const vector<double> &maps_y)
{

	double closestLen = 100000; //large number
	int closestWaypoint = 0;

	for(int i = 0; i < maps_x.size(); i++)
	{
		double map_x = maps_x[i];
		double map_y = maps_y[i];
		double dist = distance(x,y,map_x,map_y);
		if(dist < closestLen)
		{
			closestLen = dist;
			closestWaypoint = i;
		}

	}

	return closestWaypoint;

}

int NextWaypoint(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{

	int closestWaypoint = ClosestWaypoint(x,y,maps_x,maps_y);

	double map_x = maps_x[closestWaypoint];
	double map_y = maps_y[closestWaypoint];

	double heading = atan2((map_y-y),(map_x-x));

	double angle = fabs(theta-heading);
  angle = min(2*pi() - angle, angle);

  if(angle > pi()/4)
  {
    closestWaypoint++;
  if (closestWaypoint == maps_x.size())
  {
    closestWaypoint = 0;
  }
  }

  return closestWaypoint;
}

// Transform from Cartesian x,y coordinates to Frenet s,d coordinates
vector<double> getFrenet(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int next_wp = NextWaypoint(x,y, theta, maps_x,maps_y);

	int prev_wp;
	prev_wp = next_wp-1;
	if(next_wp == 0)
	{
		prev_wp  = maps_x.size()-1;
	}

	double n_x = maps_x[next_wp]-maps_x[prev_wp];
	double n_y = maps_y[next_wp]-maps_y[prev_wp];
	double x_x = x - maps_x[prev_wp];
	double x_y = y - maps_y[prev_wp];

	// find the projection of x onto n
	double proj_norm = (x_x*n_x+x_y*n_y)/(n_x*n_x+n_y*n_y);
	double proj_x = proj_norm*n_x;
	double proj_y = proj_norm*n_y;

	double frenet_d = distance(x_x,x_y,proj_x,proj_y);

	//see if d value is positive or negative by comparing it to a center point

	double center_x = 1000-maps_x[prev_wp];
	double center_y = 2000-maps_y[prev_wp];
	double centerToPos = distance(center_x,center_y,x_x,x_y);
	double centerToRef = distance(center_x,center_y,proj_x,proj_y);

	if(centerToPos <= centerToRef)
	{
		frenet_d *= -1;
	}

	// calculate s value
	double frenet_s = 0;
	for(int i = 0; i < prev_wp; i++)
	{
		frenet_s += distance(maps_x[i],maps_y[i],maps_x[i+1],maps_y[i+1]);
	}

	frenet_s += distance(0,0,proj_x,proj_y);

	return {frenet_s,frenet_d};

}

// Transform from Frenet s,d coordinates to Cartesian x,y
vector<double> getXY(double s, double d, const vector<double> &maps_s, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int prev_wp = -1;

	while(s > maps_s[prev_wp+1] && (prev_wp < (int)(maps_s.size()-1) ))
	{
		prev_wp++;
	}

	int wp2 = (prev_wp+1)%maps_x.size();

	double heading = atan2((maps_y[wp2]-maps_y[prev_wp]),(maps_x[wp2]-maps_x[prev_wp]));
	// the x,y,s along the segment
	double seg_s = (s-maps_s[prev_wp]);

	double seg_x = maps_x[prev_wp]+seg_s*cos(heading);
	double seg_y = maps_y[prev_wp]+seg_s*sin(heading);

	double perp_heading = heading-pi()/2;

	double x = seg_x + d*cos(perp_heading);
	double y = seg_y + d*sin(perp_heading);

	return {x,y};

}

/**
 * return the minimum of two variables.
 */
static double min(double a, double b)
{
	if(a<b)
	{
		return a;
	}
	else
	{
		return b;
	}

}

/**
 * Control smoothly the speed "speed" of the car using a PD controller.
 */
static double speed_control(double cmd_speed,double speed)
{
	static double old_error = cmd_speed-speed;
	double control = 0;
	double error = 0;

	error = cmd_speed-speed;

	//cout<<"|| Error(t-1)= "<<old_error<<" || Error(t)= "<<error<<endl;

	if(speed>MAX_SP)
	{
		control = MAX_SP;
	}
	else
	{
		// PD Controller
		control = speed +0.2*(error)+0.5*(error-old_error);
		// saturate the control action to account for abrupt changes
		if(abs(control - speed)>3.9)
		{
			control = speed + 3.9*abs(control-speed)/(control-speed);
		}
		control = min(MAX_SP,control);
	}
	old_error = error;

	return control;
}

/**
 * Keep the desired distance "s" given the car speed "speed"
 * and the distance "mins" to the closest car.
 * A PD Controller is also here used.
 */
static double keep_distance(double s, double mins,double speed)
{
	static double old_error = s-mins;
	double error = s-mins;
	double sp;

	// Max acceleration if the closest car is more far than the desired distance
	if(mins>s)
	{
		sp = MAX_SP;
	}
	else{
	//PD Controller
	sp = mins - 0.08*abs(error) -1.7*abs(error-old_error);
	// Speed saturation
	sp = min(sp,MAX_SP);
	}
	old_error = error;

	return speed_control(sp,speed);

}

/**
 * State machine for path planning decisions
 */
static double plan_sm(CAR_STATE_T &car_state)
{
	static double mov_sp;
	static double change_cnt = 255; // change counter

	switch(car_state.path)
	{
	case KEEP_LANE:
	{
		// keep 50m distance to the closest car
		mov_sp = keep_distance(42,car_state.min_dst,car_state.speed);


		// A car is detected in the car lane
		if((car_state.sensors[1]>0)&&(car_state.min_dst<42)&&(car_state.min_dst>25)&&(car_state.speed>34))
		{
			// Change to left lane if no car is detected there
			if((car_state.sensors[0] == 0)&&(car_state.lane >0) &&(change_cnt ==255))
			{
			change_cnt = 0;
			car_state.path = CHANGE_L;
			}
			// Change to right lane if no car is detected there
			else if((car_state.sensors[2] == 0)&&(car_state.lane <2) &&(change_cnt ==255))
			{
				change_cnt = 0;
				car_state.path = CHANGE_R;
			}
		}
		break;
	}

	case CHANGE_L:
	{
		// Accelerate and change lane
		if(change_cnt <20)
		{
			//change lane
			if(change_cnt==0)
			{
			car_state.des_lane =car_state.lane-1;
			}
			mov_sp = speed_control(min(car_state.speed +5,MAX_SP),car_state.speed);
			car_state.spx = car_state.speed+8;
			change_cnt++;
		}
		// Lane change is in progress
		else if(change_cnt<100)
		{
			mov_sp = keep_distance(35,car_state.min_dst,car_state.speed);
			if(car_state.spx>30)
			{
				car_state.spx-=0.4;
			}
			change_cnt ++;
		}
		else{
			car_state.spx = 30;
			car_state.path = KEEP_LANE;
			change_cnt = 255;
		}
		break;
	}
	case CHANGE_R:
		{
			// Accelerate and change lane
			if(change_cnt < 20)
			{
				//change lane
				if(change_cnt == 0)
				{
				car_state.des_lane =car_state.lane+1;
				}
				mov_sp = speed_control(min(car_state.speed +5,MAX_SP),car_state.speed);
				car_state.spx =car_state.speed + 8;
				change_cnt++;
			}
			// Lane change is in progress
			else if(change_cnt<100)
			{
				mov_sp = keep_distance(35,car_state.min_dst,car_state.speed);
				change_cnt ++;
				if(car_state.spx>30)
				{
					car_state.spx-=0.4;
				}
			}
			else{
				car_state.spx = 30;
				car_state.path = KEEP_LANE;
				change_cnt = 255;
			}
		}
		break;

	}
	return mov_sp;
}

int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  ifstream in_map_(map_file_.c_str(), ifstream::in);

  string line;
  while (getline(in_map_, line)) {
  	istringstream iss(line);
  	double x;
  	double y;
  	float s;
  	float d_x;
  	float d_y;
  	iss >> x;
  	iss >> y;
  	iss >> s;
  	iss >> d_x;
  	iss >> d_y;
  	map_waypoints_x.push_back(x);
  	map_waypoints_y.push_back(y);
  	map_waypoints_s.push_back(s);
  	map_waypoints_dx.push_back(d_x);
  	map_waypoints_dy.push_back(d_y);
  }


  h.onMessage([&map_waypoints_x,&map_waypoints_y,&map_waypoints_s,&map_waypoints_dx,&map_waypoints_dy](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    //auto sdata = string(data).substr(0, length);
    //cout << sdata << endl;
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);


      if (s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object
          
        	// Main car's localization Data
          	double car_x = j[1]["x"];
          	double car_y = j[1]["y"];
          	double car_s = j[1]["s"];
          	double car_d = j[1]["d"];
          	double car_yaw = j[1]["yaw"];
          	double car_speed = j[1]["speed"];

          	// Previous path data given to the Planner
          	auto previous_path_x = j[1]["previous_path_x"];
          	auto previous_path_y = j[1]["previous_path_y"];
          	// Previous path's end s and d values 
          	double end_path_s = j[1]["end_path_s"];
          	double end_path_d = j[1]["end_path_d"];

          	// Sensor Fusion Data, a list of all other cars on the same side of the road.
          	auto sensor_fusion = j[1]["sensor_fusion"];

          	json msgJson;

          	vector<double> next_x_vals;
          	vector<double> next_y_vals;

          	// TODO: define a path made up of (x,y) points that the car will visit sequentially every .02 seconds

          	//cout<<"Others Cars : "<<sensor_fusion<<endl;

            static unsigned short des_lane = 1; // desired car lane
            static double des_speed = MAX_SP;
            static double mov_speed = 0;// desired speed miles/hour
            static double speed_timer = 0;

            static CAR_STATE_T car_state;

            static double spc_x = 30;
            static double center_speed = 0;
            double min_s = 100;
            double min_sp = 50;
            static int start = 0;
            CAR_STATE_T CAR_STATE;

          	// Previous path size
            int p_path_size = previous_path_x.size();


            // computing car actual lane
            car_state.lane = car_d /4;

            // Variables initialization
            car_state.speed = car_speed;
            car_state.min_dst = 100;
            car_state.sensors[0] = 0;
            car_state.sensors[1] = 0;
            car_state.sensors[2] = 0;
            // detecting the car from the sensor fusion
            for(int i=0;i<sensor_fusion.size();i++)
            {
            	double cs_s = sensor_fusion[i][5];
            	double cs_d = sensor_fusion[i][6];
            	double cs_vx = sensor_fusion[i][3];
				double cs_vy = sensor_fusion[i][4];
            	double cs_sp = sqrt(cs_vx * cs_vx + cs_vy*cs_vy);

            	/* Predicting the position of the car in the future */
            	cs_s += (double)p_path_size*0.005*cs_sp;

            	// Car in front of the car and in a 100m distance
            	if(((cs_s+9)>car_s) && (cs_s - car_s<100))
            	{
            		// Detect car in the car lane
            		if((cs_d>car_d -2)&& (cs_d<car_d +2))
            		{
            			if((cs_s-car_s) < car_state.min_dst)
            			{
            				car_state.min_dst = cs_s-car_s;
            				min_sp = cs_sp;
            			}
            			car_state.sensors[1] +=1;
            		}
            		// Detect cars right of the car lane
            		else if((cs_d >car_d +4 -2)&&(cs_d<car_d+4 +2))
            		{
            			if((cs_s - car_s)<45)
            			{
            				car_state.sensors[2] +=1;
            			}
            		}
            		// Detect cars left of the car lane
            		else if((cs_d >car_d -4 -2)&&(cs_d<car_d-4 +2))
            		{
            			if((cs_s - car_s)<45)
            			{
            				car_state.sensors[0] +=1;
            			}
            		}
            	}

            }
        // Initialize the car SM variable for the first run
    	if(start == 0)
    	{
    		car_state.path = KEEP_LANE;
    		car_state.des_lane = car_state.lane;
    		car_state.spx = 40;
    		start = 1;
    	}

    	mov_speed = plan_sm(car_state);
    	spc_x = car_state.spx;
    	des_lane = car_state.des_lane;
    	/*
		cout<<"State="<<car_state.path<<"|| Car_lane= "<<car_state.lane<<", "<<car_state.des_lane<<"|| Speed= "<<car_state.speed<<endl;
    	cout<<"|| left= "<<car_state.sensors[0]<<"|| Center=" <<car_state.sensors[1]<<", "<<car_state.min_dst<<"|| Right= " <<car_state.sensors[2]<<endl;
    	cout<<"************************************************************************************************************************"<<endl;
    	 */
        // Widely spaced points list
         vector<double> xpts;
         vector<double> ypts;

         // desired car points start at last car position
         double des_x = car_x;
         double des_y = car_y;
         double des_yaw = deg2rad(car_yaw);

         // if the previous path continuous is empty
         if(p_path_size < 2)
         {
        	 // add the tangent points to the trajectory
        	 double tg_carx  = car_x - cos(car_yaw);
        	 double tg_cary  = car_y - sin(car_yaw);
        	 xpts.push_back(tg_carx);
        	 xpts.push_back(car_x);
        	 ypts.push_back(tg_cary);
        	 ypts.push_back(car_y);
          	}
          	else
          	{
          		des_x = previous_path_x[p_path_size - 1];
          		des_y = previous_path_y[p_path_size - 1];

          		double p_des_x = previous_path_x[p_path_size - 2];
          		double p_des_y = previous_path_y[p_path_size - 2];
          		des_yaw = atan2(des_y-p_des_y,des_x - p_des_x);

          		xpts.push_back(p_des_x);
          		xpts.push_back(des_x);

          		ypts.push_back(p_des_y);
          		ypts.push_back(des_y);
          	}

          	// adding equally spaced points:

          	vector<double> next_pt1 = getXY(car_s+ spc_x, 2+ 4 * des_lane , map_waypoints_s, map_waypoints_x, map_waypoints_y);
          	vector<double> next_pt2 = getXY(car_s+ 2*spc_x, 2+ 4 * des_lane , map_waypoints_s, map_waypoints_x, map_waypoints_y);
          	vector<double> next_pt3 = getXY(car_s+ 3*spc_x, 2+ 4 * des_lane , map_waypoints_s, map_waypoints_x, map_waypoints_y);
          	vector<double> next_pt4 = getXY(car_s+ 4*spc_x, 2+ 4 * des_lane , map_waypoints_s, map_waypoints_x, map_waypoints_y);
          	//vector<double> next_pt5 = getXY(car_s+ 5*spc_x, 2+ 4 * des_lane , map_waypoints_s, map_waypoints_x, map_waypoints_y);

          	xpts.push_back(next_pt1[0]);
          	xpts.push_back(next_pt2[0]);
          	xpts.push_back(next_pt3[0]);
          	xpts.push_back(next_pt4[0]);
          	//xpts.push_back(next_pt5[0]);

          	ypts.push_back(next_pt1[1]);
          	ypts.push_back(next_pt2[1]);
          	ypts.push_back(next_pt3[1]);
          	ypts.push_back(next_pt4[1]);
          	//ypts.push_back(next_pt5[1]);




          	// Shifting the points
            for(int i = 0; i < xpts.size(); i++)
            {
            	double x_shift = xpts[i]- des_x;
            	double y_shift = ypts[i]- des_y;

            	xpts[i] = (x_shift *cos(0-des_yaw) - y_shift * sin(0-des_yaw));
            	ypts[i] = (x_shift *sin(0-des_yaw) + y_shift * cos(0-des_yaw));

            }

            // create spline
            tk::spline spl;

            // adding the (x, y) points to the spline
            spl.set_points(xpts, ypts);

            // adding the previous path points to next path
            for(int i =0;i<previous_path_x.size();i++)
            {
            	next_x_vals.push_back(previous_path_x[i]);
            	next_y_vals.push_back(previous_path_y[i]);
            }

            double spc_y = spl(spc_x);
            double spc_dis = sqrt(spc_x*spc_x + spc_y*spc_y);
            double add_x = 0;
            // converting form miles per hour to meter per second.
            double MPH_TO_MPS = 1609.344/(60*60);


            for(int i=0; i<= 50-p_path_size;i++)
            {
            	double N = spc_dis/ (0.02 * mov_speed*MPH_TO_MPS);
            	double point_x = add_x + spc_x/N;
            	double point_y = spl(point_x);

            	add_x = point_x;

            	double des_x1 = point_x;
            	double des_y1 = point_y;

            	// converting
            	point_x = des_x1*cos(des_yaw) - des_y1*sin(des_yaw);
            	point_y = des_x1*sin(des_yaw) + des_y1*cos(des_yaw);

            	point_x += des_x;
            	point_y += des_y;

            	next_x_vals.push_back(point_x);
            	next_y_vals.push_back(point_y);
            }
            // END TODO


          	msgJson["next_x"] = next_x_vals;
          	msgJson["next_y"] = next_y_vals;


          	auto msg = "42[\"control\","+ msgJson.dump()+"]";

          	//this_thread::sleep_for(chrono::milliseconds(1000));
          	ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
          
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
