/*
 * File: slBenchmark.cpp
 * 
 * Copyright 2016 Evan Dekker
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Description:
 *
 * This file implements classes slBenchmark and slImplementation. The
 * slBenchmark class conducts the structured light scanning tests
 * and stores various benchmarking information. The slImplementation
 * class allows a custom structured light implementation to be
 * tested.
 */ 
#include "slBenchmark.h"

//Cross platform mkdir
#ifdef _WIN32
#include <direct.h>
#endif

int makeDir(const char* name) {
#ifdef _WIN32
	return mkdir(name);
#else
	return mkdir(name, S_IRWXU);
#endif
}

/*
 * slImplementation
 */ 

//Create a structured light implementation
slImplementation::slImplementation(string newIdentifier): identifier(newIdentifier), experiment(NULL) {
}

//Set the identifier
void slImplementation::setIdentifier(string newIdentifier) {
	identifier = newIdentifier;
}

//Get the pattern x offset factor, can account for uneven column widths
double slImplementation::getPatternXOffsetFactor(double xPattern) {
	return xPattern / getPatternWidth();
}

//Get the identifier
string slImplementation::getIdentifier() {
	return identifier;
}

//Check if there are any more pattern generation and capture iterations
bool slImplementation::hasMoreIterations() {
	return experiment->getIterationIndex() == 0;
}

//Process after the interations
void slImplementation::postIterationsProcess() {
	Size cameraResolution = experiment->getInfrastructure()->getCameraResolution();
	Size projectorResolution = experiment->getInfrastructure()->getProjectorResolution();

	for (int y = 0; y < cameraResolution.height; y++) {
		for (int xPattern = 0; xPattern < getPatternWidth(); xPattern++) {
			double xCamera = solveCorrespondence(xPattern, y);	

			if (!isnan(xCamera) && xCamera != -1) {					
				double displacement = experiment->getDisplacement(xPattern, xCamera);
				int xProjector = (int)(experiment->getImplementation()->getPatternXOffsetFactor(xPattern) * projectorResolution.width);

				if (!isinf(displacement)) {
					slDepthExperimentResult result(xProjector, y, displacement);
					experiment->storeResult(&result);
				}
			}
		}
	}
}

/*
 * slInfrastructure
 */ 

//Initialise the infrastructure
void slInfrastructure::init() {
	//Read calbration matricies
	stringstream filename;
	filename << getUniqueID() << ".xml";

	ifstream file(filename.str().c_str());
	if (file.good()) {
		FileStorage fileStorage(filename.str().c_str(), FileStorage::READ);
	
		fileStorage[INTRINSIC_NAME] >> intrinsicMat;
		fileStorage[DISTORTION_NAME] >> distortionMat;

		fileStorage.release();
	} else {
		cout << "Calibration for infrastruture " << getName() << " and setup not found, calibrate now? (please ensure projected checkerboard pattern can be captured by camera) [y,n]" << endl;
		char input;
		cin >> input;

		if (input == 'y' || input == 'Y') {			
			Mat chessboardMat;

			int border = 20;
			Size projectorResolution = getProjectorResolution();

			int squareHeight = (int)floor((projectorResolution.height - (border * 2)) / 7);
			int squareWidth = (int)floor((projectorResolution.width - (border * 2)) / 10);

			int squareSize = squareHeight < squareWidth ? squareHeight : squareWidth;

			chessboardMat.create((int)projectorResolution.height, (int)projectorResolution.width, CV_8UC3);
			chessboardMat.setTo(Scalar(255, 255, 255));

			for (int x = 0; x < 10; x++) {
				for (int y = 0; y < 7; y++) {					
					if ((x % 2 == 0 && y % 2 != 0) || (x % 2 != 0 && y % 2 == 0)) {
						rectangle(chessboardMat, Point((x * squareSize) + border, (y * squareSize) + border), Point(((x + 1) * squareSize) + border, ((y + 1) * squareSize) + border), Scalar(0, 0, 0), FILLED);
					}
				}
			}

			Mat capturedChessboardMat = projectAndCapture(chessboardMat);
			Mat grayCapturedChessboardMat;
			cvtColor(capturedChessboardMat, grayCapturedChessboardMat, CV_BGR2GRAY);

//			imwrite("chessboard.png", grayCapturedChessboardMat);

			int numCornersHor = 9;
			int numCornersVer = 6;

		    	int numSquares = numCornersHor * numCornersVer;
			Size boardSize = Size(numCornersHor, numCornersVer);

			vector<vector<Point3f> > objectPoints;
			vector<vector<Point2f> > imagePoints;

			vector<Point2f> corners;

			vector<Point3f> obj;
			for (int j = 0; j < numSquares; j++) {
				obj.push_back(Point3f(j / numCornersHor, j % numCornersHor, 0.0f));
			}


			if (findChessboardCorners(
				grayCapturedChessboardMat, boardSize, corners, CV_CALIB_CB_ADAPTIVE_THRESH)
			) {
				cornerSubPix(grayCapturedChessboardMat, corners, Size(11, 11), Size(-1, -1), TermCriteria(CV_TERMCRIT_EPS | CV_TERMCRIT_ITER, 30, 0.1));
			} else {
				FATAL("Could not find chessboard corners during calibration. Please ensure the camera can capture the projector output.")
			}

			imagePoints.push_back(corners);
			objectPoints.push_back(obj);

			intrinsicMat = Mat(3, 3, CV_32FC1);
			vector<Mat> rvecs;
			vector<Mat> tvecs;

			intrinsicMat.ptr<float>(0)[0] = 1;
			intrinsicMat.ptr<float>(1)[1] = 1;

			calibrateCamera(objectPoints, imagePoints, capturedChessboardMat.size(), intrinsicMat, distortionMat, rvecs, tvecs);

			FileStorage fs(filename.str().c_str(), FileStorage::WRITE);
			fs << INTRINSIC_NAME << intrinsicMat;
			fs << DISTORTION_NAME << distortionMat;
			fs.release();

		} else {
			FATAL("Cannot continue without calibration completed.")
		}
	}
}

//The name of this infrastructure
string slInfrastructure::getName() {
	return name;
}

//Get the camera resolution
Size slInfrastructure::getCameraResolution() {
	return infrastructureSetup.cameraDevice.resolution;
}

//Get the projector resolution
Size slInfrastructure::getProjectorResolution() {
	return infrastructureSetup.projectorDevice.resolution;
}

//Get the camera horizontal FOV angle (degrees)
double slInfrastructure::getCameraHorizontalFOV() {
	return infrastructureSetup.cameraDevice.horizontalFOV;
}

//Get the camera vertical FOV angle (degrees)
double slInfrastructure::getCameraVerticalFOV() {
	return infrastructureSetup.cameraDevice.verticalFOV;
}

//Get the projector horizontal FOV angle (degrees)
double slInfrastructure::getProjectorHorizontalFOV() {
	return infrastructureSetup.projectorDevice.horizontalFOV;
}

//Get the projector vertical FOV angle (degrees)
double slInfrastructure::getProjectorVerticalFOV() {
	return infrastructureSetup.projectorDevice.verticalFOV;
}

//Get the distance between the camera and the projector
double slInfrastructure::getCameraProjectorSeparation() {
	return infrastructureSetup.cameraProjectorSeparation;
}

//Generate a unique identifier for this infrastructure and setup (for saving/reading calibration)
unsigned int slInfrastructure::getUniqueID() {
	unsigned int hash = 0;
	unsigned int x    = 0;
	unsigned int i    = 0;

	stringstream id;

	id << 
		getName() << "-" <<
		getCameraResolution() << "-" <<
		getCameraHorizontalFOV() << "-" <<
		getCameraVerticalFOV() << "-" <<
		getProjectorResolution() << "-" <<
		getProjectorHorizontalFOV() << "-" <<
		getProjectorVerticalFOV() << "-" <<
		getCameraProjectorSeparation();

	string idStr = id.str();
	const char *str = idStr.c_str();

	for (i = 0; i < idStr.length(); ++str, ++i) {
		hash = (hash << 4) + (*str);

		if ((x = hash & 0xF0000000L) != 0) {
			hash ^= (x >> 24);
		}

		hash &= ~x;
	}

	return hash;
}

/*
 * slBlenderVirtualInfrastructure
 */ 

//Initialise the infrastructure
void slBlenderVirtualInfrastructure::init() {
	string tempVirtualSceneJSONFilename = virtualSceneJSONFilename;
	virtualSceneJSONFilename = string("slVirtualScene.json.CALIBRATE");
	
	slInfrastructure::init();

	virtualSceneJSONFilename = tempVirtualSceneJSONFilename;
}

//Project the structured light implementation pattern and capture it
Mat slBlenderVirtualInfrastructure::projectAndCapture(Mat patternMat) {
	DB("-> slBlenderVirtualInfrastructure::projectAndCapture()")

	stringstream patternFilename, captureFilename, outputFilename, blenderCommandLine;

	patternFilename << "." << OS_SEP << "blender_tmp_pattern.png";
	captureFilename << "." << OS_SEP << "blender_tmp_capture.png";
	outputFilename << experiment->getPath() << OS_SEP << "slVirtualScene_" << experiment->getIterationIndex() << ".blend";

	imwrite(patternFilename.str().c_str(), patternMat);

	blenderCommandLine 
		<< "blender -b -P slBlenderVirtualInfrastructure.py -- " 
			<< patternFilename.str() << " " 
			<< captureFilename.str() << " " 
			<< outputFilename.str() << " "
			<< (int)getCameraResolution().width << " " 
			<< (int)getCameraResolution().height << " "
			<< getCameraHorizontalFOV() << " "
			<< getProjectorHorizontalFOV() << " "
			<< getCameraProjectorSeparation() << " "
			<< (saveBlenderFile ? "true" : "false") << " "
			<< virtualSceneJSONFilename;
			
	DB("blenderCommandLine: " << blenderCommandLine.str())

	int exeResult = system(blenderCommandLine.str().c_str());
	DB("exeResult: " << exeResult)

	if (exeResult != 0) {
		FATAL("Could not launch blender. Please ensure the blender executable can be found in the current path.")
	}

	Mat captureMat = imread(captureFilename.str().c_str());

	remove(patternFilename.str().c_str());
	remove(captureFilename.str().c_str());
	
	DB("<- slBlenderVirtualInfrastructure::projectAndCapture()")

	return captureMat;
}

/*
 * slPhysicalInfrastructure
 */ 

//Create a physical infrastruture instance
slPhysicalInfrastructure::slPhysicalInfrastructure(slInfrastructureSetup newInfrastructureSetup, int newWaitTime): slInfrastructure(string("slPhysicalInfrastructure"), newInfrastructureSetup), waitTime(newWaitTime) {
	slCameraDevice cameraDevice = infrastructureSetup.cameraDevice;
	bool isPipe = cameraDevice.cameraPipe.length() > 0;

	if (isPipe) {
		videoCapture = VideoCapture(cameraDevice.cameraPipe.c_str());
	} else {
		videoCapture = VideoCapture(cameraDevice.cameraIndex);
	}

	if (!videoCapture.isOpened()) {
		if (isPipe) {
			FATAL("Could not open gstreamer pipe: \"" << cameraDevice.cameraPipe << "\"")
		} else {
			FATAL("Could not open camera index: " << cameraDevice.cameraIndex)
		}
	}
	
}

//Clean up
slPhysicalInfrastructure::~slPhysicalInfrastructure() {
	videoCapture.release();
}

//Project the structured light implementation pattern and capture it
Mat slPhysicalInfrastructure::projectAndCapture(Mat patternMat) {
	DB("-> slPhysicalInfrastructure::projectAndCapture()")

	Mat captureMat;

	namedWindow("main", CV_WINDOW_NORMAL);
	setWindowProperty("main", CV_WND_PROP_FULLSCREEN, CV_WINDOW_FULLSCREEN);
	
	imshow("main", patternMat);

	waitKey(waitTime);

	videoCapture >> captureMat;

	waitKey(waitTime);

	DB("<- slPhysicalInfrastructure::projectAndCapture()")

	return captureMat;
}

/*
 * slFileInfrastructure
 */ 

//Project the structured light implementation pattern and capture it
Mat slFileInfrastructure::projectAndCapture(Mat patternMat) {
	DB("-> slFileInfrastructure::projectAndCapture()")
			
	stringstream captureFilename;

	captureFilename << experiment->getImplementation()->getIdentifier() << OS_SEP << "capture_" << experiment->getIterationIndex() << ".png" ;
	DB("reading file " << captureFilename.str().c_str());
	Mat captureMat;
	ifstream file(captureFilename.str().c_str());
	if (file.good()) {
		captureMat = imread(captureFilename.str().c_str());
	} else {
		DB("WARNING: file \"" << captureFilename.str() << "\" does not exist")
	}
	file.close();
	DB("<- slFileInfrastructure::projectAndCapture()")

	return captureMat;
}

/*
 * slExperiment
 */ 

//Set default session path
string slExperiment::sessionPath = string("");

//Get the current session path
string slExperiment::getSessionPath() {
	if (sessionPath.empty()) {
		stringstream pathStream;

		pathStream << "." << OS_SEP << clock() << OS_SEP;
		sessionPath =  pathStream.str();

		makeDir(sessionPath.c_str());
	}
	
	return sessionPath;
}

//Create an experiment
slExperiment::slExperiment(slInfrastructure *newlInfrastructure, slImplementation *newImplementation) : infrastructure(newlInfrastructure), implementation(newImplementation) {
	path = string("");
	captures = new vector<Mat>();
}

//Clean up
slExperiment::~slExperiment() {
	delete captures;
}

//Get the current experiment path
string slExperiment::getPath() {
	if (path.empty()) {
		stringstream pathStream;

		pathStream << getSessionPath() << getIdentifier() << clock() << OS_SEP;
		path =  pathStream.str();

		makeDir(path.c_str());
	}
	
	return path;
}

//Run this experiment
void slExperiment::run() {
	DB("-> slExperiment::run() infrastructure: " << infrastructure->getName() << " implementation: " << implementation->getIdentifier())

	//Set the current experiments of the infrastructre and implementation to this experiment
	infrastructure->experiment = this;
	implementation->experiment = this;

	//Initialise the infrastructure
	infrastructure->init();

	//Inform the implementation the experiment is about to run
	implementation->preExperimentRun();

	//String paths for the current implementation
	stringstream patternsPathStream, capturesPathStream, patternFileStream, captureFileStream;

	patternsPathStream << getPath() << "patterns";
	capturesPathStream << getPath() << "captures";

	makeDir(patternsPathStream.str().c_str());
	makeDir(capturesPathStream.str().c_str());

	//Zero the iteration index
	iterationIndex = 0;

	//Run before all iterations begin
	runPreIterations();
		
	//Loop until the structured light implementation's pattern generation and capture iterations are completed
	while (implementation->hasMoreIterations()) {
		//Run before this iteration begins
		runPreIteration();
		
		DB("About to start iteration #" << iterationIndex << "...")

		patternFileStream.str("");
		captureFileStream.str("");


			
		//Generate the implementation's pattern
		DB("About to implementation->generatePattern()...")

		//Run before a pattern is generated
		runPrePatternGeneration();

		Mat patternMat = implementation->generatePattern();

		//Run after a pattern is generated
		runPostPatternGeneration();

		DB("implementation->generatePattern() complete.")



		//Create current pattern file path
		patternFileStream << patternsPathStream.str() << OS_SEP << "pattern_" << iterationIndex << ".png";

		//Save the pattern to the implementation's patterns
		imwrite(patternFileStream.str(), patternMat);



		//Capture the implementation's pattern using the current infrastructure
		DB("About to infrastructure->projectAndCapture()...")

		//Run before pattern is projected and captured
		runPreProjectAndCapture();

		Mat captureMat = infrastructure->projectAndCapture(patternMat);

		//Run after pattern is projected and captured
		runPostProjectAndCapture();

		//Undistort the capture
		Mat undistortedCaptureMat;
		undistort(captureMat, undistortedCaptureMat, infrastructure->intrinsicMat, infrastructure->distortionMat);

		DB("infrastructure->projectAndCapture() complete.")



		//Create current capture file path
		captureFileStream << capturesPathStream.str() << OS_SEP << "capture_" << iterationIndex << ".png";

		//Save the capture to the implementation's captures
		imwrite(captureFileStream.str(), undistortedCaptureMat);



		//Allow the implementation to process the capture
		DB("About to implementation->processCapture()...")

		//Run before the implementation processes this capture
		runPreProcessCapture();

		implementation->processCapture(undistortedCaptureMat);
		//implementation->processCapture(captureMat);

		//Run after the implementation processes this capture
		runPostProcessCapture();

		DB("implementation->processCapture() complete.")



		DB("Iteration #" << iterationIndex << " complete.")



		//Run after this iteration has completed
		runPostIteration();

		//Increment the iteration index
		iterationIndex++;

	}

	//Run after all iterations have completed
	runPostIterations();


		
	//Allow the implementation to post process after the iterations
	DB("About to implementation->postIterationsProcess()...")

	//Run before the implementation processes after all the iterations
	runPreImplementationPostIterationsProcess();

	implementation->postIterationsProcess();

	//Run after the implementation processes after all the iterations
	runPostImplementationPostIterationsProcess();

	DB("implementation->postIterationsProcess() complete.")



	//Inform the implementation the experiment has completed running
	implementation->postExperimentRun();

	//Unset the current experiments of the infrastructre and implementation
	infrastructure->experiment = NULL;
	implementation->experiment = NULL;

	DB("<- slExperiment::end()")
}

//Get the current infrastructure
slInfrastructure *slExperiment::getInfrastructure() {
	return infrastructure;
}

//Get the current implementation
slImplementation *slExperiment::getImplementation() {
	return implementation;
}

//Get the current pattern generation and capture iteration index
int slExperiment::getIterationIndex() {
	return iterationIndex;
}

//Store the capture
void slExperiment::storeCapture(Mat captureMat) {
	captures->push_back(captureMat);
}

//Get the capture at an index
Mat slExperiment::getCaptureAt(int index) {
	return captures->at(index);
}

//Get the last capture
Mat slExperiment::getLastCapture() {
	return captures->back();
}

//Get the number of captures
int slExperiment::getNumberCaptures() {
	return captures->size();
}

//Get a meaningful identifier of this experiment
string slExperiment::getIdentifier() {
	stringstream identifierStream;

	identifierStream <<  infrastructure->getName() << implementation->getIdentifier();
//	identifierStream << "Experiment infrastructure: " << infrastructure->getName() << " implementation: " << implementation->getIdentifier();

	return identifierStream.str();
}

double slExperiment::getDisplacement(double x_pattern, double x_image) {
	return getDisplacement(x_pattern, x_image, false);
}
double slExperiment::getDisplacement(double x_pattern, double x_image, bool temp) {
    // Proper calculation of displacement depends on the following parameters:
    // * depth of view of the camera and of the projector
    // * Resolution of the camera and the projector
    // Optionally, we need also the following parameter:
    // * distance between the camera and the project.
    // Setting this will give an accurate depth. Otherwise, proportions 
    // should be correct, but not to scale.
    double xc = x_image/infrastructure->getCameraResolution().width - 0.5;
    double xp = x_pattern/implementation->getPatternWidth() - 0.5;
    double piOn180 = M_PI/180;
    double gammac = infrastructure->getCameraHorizontalFOV() * piOn180; // depth of camera view in radians.
    double gammap = infrastructure->getProjectorHorizontalFOV() * piOn180; // depths of projector view in radians.
    double tgc = tan(gammac/2), tgp = tan(gammap/2);
    double Delta = infrastructure->getCameraProjectorSeparation(); // Distance between camera and projector
/*
if (temp) {
	DB("xc[" << xc << "] = x_image[" << x_image << "]/getCaptureWidth()[" << getCaptureWidth() << "] - 0.5")
	DB("xp[" << xp << "] = x_pattern[" << x_pattern << "]/getPatternWidth()[" << getPatternWidth() << "] - 0.5")
	DB("tgc[" << tgc << "] = tan(gammac[" << gammac << "]/2), tgp[" << tgp << "] = tan(gammap[" << gammap << "]/2")
}
*/
	double displacement = Delta / 2 / (tgp*xp - tgc*xc);
/*	
	if (displacement == 0) {
		DB("x_pattern: " << x_pattern << " x_image: " << x_image)
	}
*/	
    return Delta / 2 / (tgp*xp - tgc*xc);
}

/*
 * slDepthExperiment
 */ 

//Create a depth experiment
slDepthExperiment::slDepthExperiment(slInfrastructure *newlInfrastructure, slImplementation *newImplementation) : slExperiment(newlInfrastructure, newImplementation)/*, depthData(NULL)*/ {
/*
	//numDepthDataValues = infrastructure->getProjectorResolution().width * infrastructure->getCameraResolution().height;
	numDepthDataValues = implementation->getPatternWidth() * infrastructure->getCameraResolution().height;

	depthDataValued = new bool[numDepthDataValues];
	depthData = new double[numDepthDataValues];

	for (int index = 0; index < numDepthDataValues; index++) {
		depthDataValued[index] = false;
		depthData[index] = 0.0;
	}
*/
	int width = implementation->getPatternWidth();
	int height = infrastructure->getCameraResolution().height;

	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
			depthDataValued[x][y] = false;
			depthData[x][y] = 0.0;
		}
	}
}

//Clean up
slDepthExperiment::~slDepthExperiment() {
/*
	if (depthData != NULL) {
		delete[] depthDataValued;
		delete[] depthData;
	}
*/
}

//Store a result of this experiment
void slDepthExperiment::storeResult(slExperimentResult *experimentResult) {
	slDepthExperimentResult *depthExperimentResult = (slDepthExperimentResult *)experimentResult;
/*
	//int arrayOffset = (depthExperimentResult->y * infrastructure->getProjectorResolution().width) + depthExperimentResult->x;
	int arrayOffset = (depthExperimentResult->y * implementation->getPatternWidth()) + depthExperimentResult->x;
	
	depthDataValued[arrayOffset] = false;
	//depthData[arrayOffset] = depthExperimentResult->z;
*/
	depthDataValued[depthExperimentResult->x][depthExperimentResult->y] = true;
	depthData[depthExperimentResult->x][depthExperimentResult->y] = depthExperimentResult->z;
}
/*
//Get the number of depth data values
int slDepthExperiment::getNumDepthDataValues() {
	return numDepthDataValues;
}
*/
//Check if depth data value has been set
//bool slDepthExperiment::isDepthDataValued(int index) {
bool slDepthExperiment::isDepthDataValued(int x, int y) {
	//return depthDataValued[index];
	return depthDataValued[x][y];
}

//Get depth data value
//double slDepthExperiment::getDepthData(int index) {
double slDepthExperiment::getDepthData(int x, int y) {
	//return depthData[index];
	return depthData[x][y];
}

/*
 * slDepthExperimentResult
 */ 

//Create a depth experiment result
slDepthExperimentResult::slDepthExperimentResult(int newX, int newY, double newZ) : x(newX), y(newY), z(newZ) {
}

/*
 * slSpeedExperiment
 */

//Create a speed experiment
slSpeedExperiment::slSpeedExperiment(slInfrastructure *newlInfrastructure, slImplementation *newImplementation) : 
	slExperiment(newlInfrastructure, newImplementation),
	previousClock(0), 
	totalClock(0) {
}

//Run before a pattern is generated
void slSpeedExperiment::runPrePatternGeneration() {
	previousClock = clock();
}

//Run after a pattern is generated
void slSpeedExperiment::runPostPatternGeneration() {
	totalClock += clock() - previousClock;
}

//Run before pattern is projected and captured
void slSpeedExperiment::runPreProjectAndCapture() {
	previousClock = clock();
}

//Run after pattern is projected and captured
void slSpeedExperiment::runPostProjectAndCapture() {
	totalClock += clock() - previousClock;
}

//Run before the implementation processes this iteration
void slSpeedExperiment::runPreIterationProcess() {
	previousClock = clock();
}

//Run after the implementation processes this iteration
void slSpeedExperiment::runPostIterationProcess() {
	totalClock += clock() - previousClock;
}

//Run before the implementation processes after all the iterations
void slSpeedExperiment::runPrePostIterationsProcess() {
	previousClock = clock();
}

//Run after the implementation processes after all the iterations
void slSpeedExperiment::runPostPostIterationsProcess() {
	totalClock += clock() - previousClock;
}

//Get the total clock value
clock_t slSpeedExperiment::getTotalClock() {
	return totalClock;
}

/*
 * slSpeedDepthExperiment
 */ 

//Create a speed and depth experiment
slSpeedDepthExperiment::slSpeedDepthExperiment(slInfrastructure *newlInfrastructure, slImplementation *newImplementation) : 
	slExperiment(newlInfrastructure, newImplementation), 
	slSpeedExperiment(newlInfrastructure, newImplementation), 
	slDepthExperiment(newlInfrastructure, newImplementation) {
}

/*
 * slBenchmark
 */ 

//Create a structured light benchmark given a reference experiment
slBenchmark::slBenchmark(slExperiment *newReferenceExperiment) : referenceExperiment(newReferenceExperiment) {
	metrics = new vector<slMetric *>();
	experiments = new vector<slExperiment *>();
}

//Clean up
slBenchmark::~slBenchmark() {
	metrics->clear();

	delete metrics;
	delete experiments;
	
}

//Add a metric to this benchmark
void slBenchmark::addMetric(slMetric *newMetric) {
	metrics->push_back(newMetric);
}

//Add an experiment to this benchmark
void slBenchmark::addExperiment(slExperiment *newExperiment) {
	experiments->push_back(newExperiment);
}

//Compare the experiments of this benchmark
void slBenchmark::compareExperiments() {
	for (vector<slMetric *>::iterator metric = metrics->begin(); metric != metrics->end(); ++metric) {
		for (vector<slExperiment *>::iterator experiment = experiments->begin(); experiment != experiments->end(); ++experiment) {
			(*metric)->compareExperimentAgainstReference((*experiment), referenceExperiment);
		}
	}
}

/*
 * slSpeedMetric
 */ 

//Compare an experiment against the reference experiment
void slSpeedMetric::compareExperimentAgainstReference(slExperiment *experiment, slExperiment *referenceExperiment) {
	slSpeedExperiment *referenceSpeedExperiment = dynamic_cast<slSpeedExperiment *>(referenceExperiment);
	slSpeedExperiment *speedExperiment = dynamic_cast<slSpeedExperiment *>(experiment);

	double speedDifference = referenceSpeedExperiment->getTotalClock() - speedExperiment->getTotalClock();

	DB("Ref: " << referenceSpeedExperiment->getIdentifier() << " totalClock: " << referenceSpeedExperiment->getTotalClock() << " (" << ((double)referenceSpeedExperiment->getTotalClock() / (double)CLOCKS_PER_SEC) << " seconds)")
	DB(speedExperiment->getIdentifier() << " totalClock: " << speedExperiment->getTotalClock() << " (" << ((double)speedExperiment->getTotalClock() / (double)CLOCKS_PER_SEC) << " seconds)")
	DB("Difference totalClock: " << speedDifference << " (" << (speedDifference / (double)CLOCKS_PER_SEC) << " seconds)")
}

/*
 * slAccuracyMetric
 */ 

//Compare an experiment against the reference experiment
void slAccuracyMetric::compareExperimentAgainstReference(slExperiment *experiment, slExperiment *referenceExperiment) {
	slDepthExperiment *referenceDepthExperiment = dynamic_cast<slDepthExperiment *>(referenceExperiment);
	slDepthExperiment *depthExperiment = dynamic_cast<slDepthExperiment *>(experiment);
/*
	int referenceCameraHeight = referenceDepthExperiment->getInfrastructure()->getCameraResolution().height;

	if (referenceCameraHeight != depthExperiment->getInfrastructure()->getCameraResolution().height) {
		DB("ERROR: To compare depth accuracy, both experiments need to have the same camera height.")
		return;
	}

	double referencePatternWidth = referenceDepthExperiment->getImplementation()->getPatternWidth(); 
	double patternWidth = depthExperiment->getImplementation()->getPatternWidth(); 

	double totalDifference = 0.0;

	int referenceWidthOffset = 0;

	for (int cameraY = 0; cameraY < referenceCameraHeight; cameraY++) {
		for (int patternX = 0; patternX < patternWidth; patternX++) {
			referenceWidthOffset += depthExperiment->getImplementation()->getReferenceColumnWidth(cameraY, patternX);

			int referenceArrayOffset = (cameraY * referencePatternWidth) + referenceWidthOffset;
			int arrayOffset = (cameraY * patternWidth) + patternX;

			if (referenceDepthExperiment->isDepthDataValued(referenceArrayOffset) && depthExperiment->isDepthDataValued(arrayOffset)) {
				double depthDifference = referenceDepthExperiment->getDepthData(referenceArrayOffset) - depthExperiment->getDepthDataAt(arrayOffset);
				totalDifference += depthDifference;
			}
		}
	
		referenceWidthOffset = 0;
	}

	DB("Ref: " << referenceDepthExperiment->getIdentifier() << " vs " << depthExperiment->getIdentifier() << " accuracy diff: " << totalDifference)
*/


	Size referenceCameraResolution = referenceDepthExperiment->getInfrastructure()->getCameraResolution();
	Size referenceProjectorResolution = referenceDepthExperiment->getInfrastructure()->getProjectorResolution();
	Size cameraResolution = depthExperiment->getInfrastructure()->getCameraResolution();
	Size projectorResolution = depthExperiment->getInfrastructure()->getProjectorResolution();

	if (referenceProjectorResolution.width != projectorResolution.width || referenceCameraResolution.height != cameraResolution.height) {
		DB("ERROR: To compare depth accuracy, both experiments need to have the same projector width and camera height.")
		return;
	}


	int numPatternColumns = projectorResolution.width;
	//int numPatternColumns = depthExperiment->getImplementation()->getPatternWidth();
	int cameraHeight = cameraResolution.height;

//	double *depthDifferences = new double[depthExperiment->getNumDepthDataValues()];
	map<int, map<int, double> > depthDifferences;	
	double maxDepthDifference = numeric_limits<double>::min();
	double minDepthDifference = numeric_limits<double>::max();
/*
	for (int depthDataIndex = 0; depthDataIndex < depthExperiment->getNumDepthDataValues(); depthDataIndex++) {
		if (referenceDepthExperiment->isDepthDataValued(depthDataIndex) && depthExperiment->isDepthDataValued(depthDataIndex)) {
			depthDifferences[depthDataIndex] = referenceDepthExperiment->getDepthData(depthDataIndex) - depthExperiment->getDepthData(depthDataIndex);

			if (depthDifferences[depthDataIndex] < 0) {
				depthDifferences[depthDataIndex] = -depthDifferences[depthDataIndex];
			}

			if (depthDifferences[depthDataIndex] > maxDepthDifference) {
				maxDepthDifference = depthDifferences[depthDataIndex];
			}	
		}
	}
*/
	for (int x = 0; x < numPatternColumns; x++) {
		for (int y = 0; y < cameraHeight; y++) {
/*
			if (referenceDepthExperiment->isDepthDataValued(x, y)) {
				DB("*** RC VALUED x: " << x << " y: " << y << " ***")
			}
			if (depthExperiment->isDepthDataValued(x, y)) {
				DB("*** SL VALUED x: " << x << " y: " << y << " ***")
			}
*/
			if (referenceDepthExperiment->isDepthDataValued(x, y) && depthExperiment->isDepthDataValued(x, y)) {
//				DB("*** BOTH VALUED ***")
				depthDifferences[x][y] = referenceDepthExperiment->getDepthData(x, y) - depthExperiment->getDepthData(x, y);
/*
				if (depthDifferences[x][y] < 0) {
					depthDifferences[x][y] = -depthDifferences[x][y];
				}
*/
				if (depthDifferences[x][y] > maxDepthDifference) {
					maxDepthDifference = depthDifferences[x][y];
				}	
				if (depthDifferences[x][y] < minDepthDifference) {
					minDepthDifference = depthDifferences[x][y];
				}	
			}
		}
	}

	double binSize = 0.001;
	//double binSize = 0.2;
	//int histogramSize = (int)ceil(maxDepthDifference / binSize);
	int histogramSize = (int)ceil((maxDepthDifference - minDepthDifference) / binSize);
	//DB("maxDepthDifference: " << maxDepthDifference << " minDepthDifference: " << minDepthDifference)
	int histogram[histogramSize];

	for (int histogramIndex = 0; histogramIndex < histogramSize; histogramIndex++) {
		histogram[histogramIndex] = 0;
	}
//	DB("histogramSize: " << histogramSize)
/*
	for (int depthDataIndex = 0; depthDataIndex < depthExperiment->getNumDepthDataValues(); depthDataIndex++) {
		if (referenceDepthExperiment->isDepthDataValued(depthDataIndex) && depthExperiment->isDepthDataValued(depthDataIndex)) {
			double depthDifference = depthDifferences[depthDataIndex] / binSize;

			histogram[(int)floor(depthDifference)]++;
		}
	}
*/
	for (int x = 0; x < numPatternColumns; x++) {
		for (int y = 0; y < cameraHeight; y++) {
			if (referenceDepthExperiment->isDepthDataValued(x, y) && depthExperiment->isDepthDataValued(x, y)) {
				//double depthDifference = depthDifferences[x][y] / binSize;
				double depthDifference = (depthDifferences[x][y] - minDepthDifference) / binSize;

				histogram[(int)floor(depthDifference)]++;
			}
		}
	}

	//delete [] depthDifferences;

	stringstream historgramFileStream;
	historgramFileStream << slExperiment::getSessionPath() << referenceDepthExperiment->getIdentifier() << "_vs_" << depthExperiment->getIdentifier() << "_accuracy_histogram.csv";

	ofstream outputFileStream(historgramFileStream.str().c_str());
	
	int totalHistograms = 0;
	for (int histogramIndex = 0; histogramIndex < histogramSize; histogramIndex++) {
		totalHistograms += histogram[histogramIndex];
	}

	for (int histogramIndex = 0; histogramIndex < histogramSize; histogramIndex++) {
		//outputFileStream << ((double)histogram[histogramIndex] / (double)totalHistograms) << endl;
		//outputFileStream << (int)floor(histogramIndex + minDepthDifference) << "," << histogram[histogramIndex] << endl;
		outputFileStream << (int)floor(histogramIndex + minDepthDifference) << "," << ((double)histogram[histogramIndex] / (double)totalHistograms) << endl;
	}

	outputFileStream.close();

	DB("Accuracy histogram file: " << historgramFileStream.str().c_str())

}

/*
 * slResolutionMetric
 */ 

//Compare an experiment against the reference experiment
void slResolutionMetric::compareExperimentAgainstReference(slExperiment *experiment, slExperiment *referenceExperiment) {
	slDepthExperiment *referenceDepthExperiment = dynamic_cast<slDepthExperiment *>(referenceExperiment);
	slDepthExperiment *depthExperiment = dynamic_cast<slDepthExperiment *>(experiment);

	slInfrastructure *infrastructure = depthExperiment->getInfrastructure();
	int numPatternColumns = infrastructure->getProjectorResolution().width;
	//int numPatternColumns = depthExperiment->getImplementation()->getPatternWidth();
	int cameraHeight = infrastructure->getCameraResolution().height;

	int referenceDataValues = 0;
	int dataValues = 0;

	for (int x = 0; x < numPatternColumns; x++) {
		for (int y = 0; y < cameraHeight; y++) {
			if (referenceDepthExperiment->isDepthDataValued(x, y)) {
				referenceDataValues++;
			}
			
			if (depthExperiment->isDepthDataValued(x, y)) {
				dataValues++;
			}
		}
	}

	int resolutionDifference = referenceDataValues - dataValues;

	DB("Ref: " << referenceDepthExperiment->getIdentifier() << " vs " << depthExperiment->getIdentifier() << " resolution diff: " << resolutionDifference)
/*
	int referenceDataValues = 0;
	
	for (int depthDataIndex = 0; depthDataIndex < referenceDepthExperiment->getNumDepthDataValues(); depthDataIndex++) {
		if (referenceDepthExperiment->isDepthDataValued(depthDataIndex)) {
			referenceDataValues++;
		}
	}

	int dataValues = 0;
	
	for (int depthDataIndex = 0; depthDataIndex < depthExperiment->getNumDepthDataValues(); depthDataIndex++) {
		if (depthExperiment->isDepthDataValued(depthDataIndex)) {
			dataValues++;
		}
	}

	int resolutionDifference = referenceDataValues - dataValues;

	DB("Ref: " << referenceDepthExperiment->getIdentifier() << " vs " << depthExperiment->getIdentifier() << " resolution diff: " << resolutionDifference)
*/
}

/*
 * sl3DReconstructor
 */ 

//Write a XYZ point cloud for the given depth experiment
void sl3DReconstructor::writeXYZPointCloud(slDepthExperiment *depthExperiment) {
	stringstream pointCloudFileStream;

	pointCloudFileStream << depthExperiment->getPath() << "point_cloud.xyz";

	DB("-> sl3DReconstructor::writeXYZPointCloud() file: " << pointCloudFileStream.str().c_str())

	ofstream outputFileStream(pointCloudFileStream.str().c_str());

	slInfrastructure *infrastructure = depthExperiment->getInfrastructure();

	int numPatternColumns = infrastructure->getProjectorResolution().width;
	//int numPatternColumns = depthExperiment->getImplementation()->getPatternWidth();
	int cameraHeight = infrastructure->getCameraResolution().height;

	double halfNumPatternColumns = (double)numPatternColumns / 2.0;
	double halfCameraHeight = (double)cameraHeight / 2.0;

	double piOn180 = M_PI/180;

	double halfProjectorHorizontalFOVRadians = tan(piOn180 * (infrastructure->getProjectorHorizontalFOV() / 2.0));
	double halfCameraVerticalFOVRadians = tan(piOn180 * (infrastructure->getCameraVerticalFOV() / 2.0));

	for (int x = 0; x < numPatternColumns; x++) {
		for (int y = 0; y < cameraHeight; y++) {
			//int arrayOffset = (y * numPatternColumns) + x;

			//if (depthExperiment->isDepthDataValued(arrayOffset)) {
			if (depthExperiment->isDepthDataValued(x, y)) {
				//double zCoord = depthExperiment->getDepthData(arrayOffset);
				double zCoord = depthExperiment->getDepthData(x, y);
				double xCoord = ((double)x - halfNumPatternColumns) * zCoord * (2.0 * halfProjectorHorizontalFOVRadians / numPatternColumns);
				double yCoord = ((double)y - halfCameraHeight) * zCoord * (2.0 * halfCameraVerticalFOVRadians / cameraHeight);
//				if (zCoord > -70 && zCoord < -20) {
					outputFileStream << xCoord << " " << yCoord << " " << zCoord << endl;
//				}

				//outputFileStream << "[x: " << x << " y: " << y << "] " << xCoord << " " << yCoord << " " << zCoord << endl;
			}
		}	
	}

	outputFileStream.close();
	
	DB("<- sl3DReconstructor::writeXYZPointCloud()")
}
