/*
 *  ofxRGBDRenderer.cpp
 *  ofxRGBDepthCaptureOpenNI
 *
 *  Created by Jim on 12/17/11.
 *  Copyright 2011 FlightPhase. All rights reserved.
 *
 */

#include "ofxRGBDRenderer.h"
#include <set>

typedef struct{
	int vertexIndex;
	bool valid;
} IndexMap;

ofxRGBDRenderer::ofxRGBDRenderer(){
	//come up with better names
	xshift = 0;
	yshift = 0;
	xmult = 0;
	ymult = 0;
	xscale = 1;
	yscale = 1;
	
	edgeCull = 4000;
	simplify = 1;
	
	rotationCompensation = 0;
	
	xTextureScale = 1;
	yTextureScale = 1;

	hasDepthImage = false;
	hasRGBImage = false;

	farClip = 5000;
//	zThreshold = ofRange(1, 5000);
}

ofxRGBDRenderer::~ofxRGBDRenderer(){

}

bool ofxRGBDRenderer::setup(string calibrationDirectory){
	
	cout << "setting up renderer " << endl;
	
	if(!ofDirectory(calibrationDirectory).exists()){
		ofLogError("ofxRGBDRenderer --- Calibration directory doesn't exist: " + calibrationDirectory);
		return false;
	}
	
	depthCalibration.load(calibrationDirectory+"/depthCalib.yml");
	rgbCalibration.load(calibrationDirectory+"/rgbCalib.yml");
	
	loadMat(rotationDepthToRGB, calibrationDirectory+"/rotationDepthToRGB.yml");
	loadMat(translationDepthToRGB, calibrationDirectory+"/translationDepthToRGB.yml");
	
	
	setSimplification(1);
}

void ofxRGBDRenderer::setSimplification(int level){
	simplify = level;
	if (simplify < 0) {
		simplify = 1;
	}
	else if(simplify > 8){
		simplify == 8;
	}
	
	baseIndeces.clear();
	int w = 640 / simplify;
	int h = 480 / simplify;
	for (int y = 0; y < h-1; y++){
		for (int x=0; x < w-1; x++){
			ofIndexType a,b,c;
			a = x+y*w;
			b = (x+1)+y*w;
			c = x+(y+1)*w;
			baseIndeces.push_back(a);
			baseIndeces.push_back(b);
			baseIndeces.push_back(c);
			
			a = (x+1)+y*w;
			b = x+(y+1)*w;
			c = (x+1)+(y+1)*w;
			baseIndeces.push_back(a);
			baseIndeces.push_back(b);
			baseIndeces.push_back(c);			
		}
	}		
}

//-----------------------------------------------
int ofxRGBDRenderer::getSimplification(){
	return simplify;
}

//-----------------------------------------------
void ofxRGBDRenderer::setRGBTexture(ofBaseHasTexture& rgbImage) {
	currentRGBImage = &rgbImage;
	hasRGBImage = true;
}

void ofxRGBDRenderer::setDepthImage(unsigned short* depthPixelsRaw){
	currentDepthImage = depthPixelsRaw;
	hasDepthImage = true;
}

void ofxRGBDRenderer::setTextureScale(float xs, float ys){
	xTextureScale = xs;
	yTextureScale = ys;
}

Calibration& ofxRGBDRenderer::getDepthCalibration(){
	return depthCalibration;
}

Calibration& ofxRGBDRenderer::getRGBCalibration(){
	return rgbCalibration;
}

void ofxRGBDRenderer::update(){
	
	if(!hasDepthImage) return;
	//cout << "processing depth frame " << endl;
	
	bool debug = false;
	
	int w = 640;
	int h = 480;
	
	int start = ofGetElapsedTimeMillis();

	Point2d fov = depthCalibration.getDistortedIntrinsics().getFov();
	
	float fx = tanf(ofDegToRad(fov.x) / 2) * 2;
	float fy = tanf(ofDegToRad(fov.y) / 2) * 2;
	
	Point2d principalPoint = depthCalibration.getDistortedIntrinsics().getPrincipalPoint();
	cv::Size imageSize = depthCalibration.getDistortedIntrinsics().getImageSize();
		
	vector<IndexMap> indexMap;
	simpleMesh.clearVertices();
	simpleMesh.clearIndices();
	simpleMesh.clearTexCoords();
	simpleMesh.clearNormals();
	
	int imageIndex = 0;
	int vertexIndex = 0;
	ofVec3f pivotPoint = ofVec3f(principalPoint.x + xmult, principalPoint.y + ymult, 0);
	ofVec3f pivotAxis = ofVec3f(0,0,1);
	for(int y = 0; y < h; y+= simplify) {
		for(int x = 0; x < w; x+= simplify) {

            unsigned short z = currentDepthImage[y*w+x];
			IndexMap indx;
			if(z != 0 && z < farClip){
				float xReal = (((float) x - principalPoint.x + xmult ) / imageSize.width) * z * fx/* + xshift*/;
				float yReal = (((float) y - principalPoint.y + ymult ) / imageSize.height) * z * fy/* + yshift*/;
				indx.vertexIndex = simpleMesh.getVertices().size();
				indx.valid = true;
				ofVec3f pt = ofVec3f(xReal, yReal, z);
				//float angle, const ofVec3f& pivot, const ofVec3f& axis 
//				if(rotationCompensation != 0){
//					pt.rotate(rotationCompensation, pivotPoint, pivotAxis);
//				}
				simpleMesh.addVertex(pt);
			}
			else {
				indx.valid = false;
			}
			indexMap.push_back( indx );
		}
	}
	if(debug) cout << "unprojection " << simpleMesh.getVertices().size() << " took " << ofGetElapsedTimeMillis() - start << endl;

	if(simpleMesh.getVertices().size() < 3){
		return;
	}
	
	set<ofIndexType> calculatedNormals;
	start = ofGetElapsedTimeMillis();
	simpleMesh.getNormals().resize(simpleMesh.getVertices().size());
	
	for(int i = 0; i < baseIndeces.size(); i+=3){
		
		if(indexMap[baseIndeces[i]].valid &&
		   indexMap[baseIndeces[i+1]].valid &&
		   indexMap[baseIndeces[i+2]].valid){
			
			ofVec3f a,b,c;
			a = simpleMesh.getVertices()[indexMap[baseIndeces[i]].vertexIndex]; 
			b = simpleMesh.getVertices()[indexMap[baseIndeces[i+1]].vertexIndex]; 
			c = simpleMesh.getVertices()[indexMap[baseIndeces[i+2]].vertexIndex]; 
			if(fabs(a.z - b.z) < edgeCull && fabs(a.z - c.z) < edgeCull){
				simpleMesh.addTriangle(indexMap[baseIndeces[i]].vertexIndex, 
									   indexMap[baseIndeces[i+1]].vertexIndex,
									   indexMap[baseIndeces[i+2]].vertexIndex);
				
				if(calculatedNormals.find(indexMap[baseIndeces[i]].vertexIndex) == calculatedNormals.end()){
					//calculate normal
					simpleMesh.setNormal(indexMap[baseIndeces[i]].vertexIndex, (b-a).getCrossed(b-c).getNormalized());
					calculatedNormals.insert(indexMap[baseIndeces[i]].vertexIndex);
				}
			}
		}
	}


	if(debug) cout << "indexing  " << simpleMesh.getIndices().size() << " took " << ofGetElapsedTimeMillis() - start << endl;

	if(hasRGBImage){
		start = ofGetElapsedTimeMillis();
		
		//Mat pcMat = Mat(toCv(mesh));
		Mat pcMat = Mat(toCv(simpleMesh));
		
		//cout << "PC " << pcMat << endl;
		//	cout << "Rot Depth->Color " << rotationDepthToColor << endl;
		//	cout << "Trans Depth->Color " << translationDepthToColor << endl;
		//	cout << "Intrs Cam " << colorCalibration.getDistortedIntrinsics().getCameraMatrix() << endl;
		//	cout << "Intrs Dist Coef " << colorCalibration.getDistCoeffs() << endl;
		
		imagePoints.clear();
		projectPoints(pcMat,
					  rotationDepthToRGB, translationDepthToRGB,
					  rgbCalibration.getDistortedIntrinsics().getCameraMatrix(),
					  rgbCalibration.getDistCoeffs(),
					  imagePoints);
	
		if(debug) cout << "project points " << (ofGetElapsedTimeMillis() - start) << endl;
		
		start = ofGetElapsedTimeMillis();
		for(int i = 0; i < imagePoints.size(); i++) {
			ofVec2f textureCoord = ofVec2f(imagePoints[i].x * xTextureScale, imagePoints[i].y * yTextureScale);
			simpleMesh.addTexCoord(textureCoord);
		}
		
		if(debug) cout << "gen tex coords took " << (ofGetElapsedTimeMillis() - start) << endl;
	}
}

ofMesh& ofxRGBDRenderer::getMesh(){
	//return mesh;
	return simpleMesh;
}

void ofxRGBDRenderer::drawMesh() {
	
	if(!hasDepthImage) return;
	
	glPushMatrix();
	glScaled(1, -1, 1);
	
	glEnable(GL_DEPTH_TEST);
	if(hasRGBImage){
		currentRGBImage->getTextureReference().bind();
	}
	simpleMesh.drawFaces();
	if(hasRGBImage){
		currentRGBImage->getTextureReference().unbind();
	}
	glDisable(GL_DEPTH_TEST);
	
	glPopMatrix();
}

void ofxRGBDRenderer::drawPointCloud() {
	
	if(!hasDepthImage) return;
	
	glPushMatrix();
	glScaled(1, -1, 1);
	
	glEnable(GL_DEPTH_TEST);
	if(hasRGBImage){
		currentRGBImage->getTextureReference().bind();
	}
	simpleMesh.drawVertices();
	if(hasRGBImage){
		currentRGBImage->getTextureReference().unbind();
	}
	glDisable(GL_DEPTH_TEST);
	
	glPopMatrix();
}

void ofxRGBDRenderer::drawWireFrame() {
	
	if(!hasDepthImage) return;
	
	glPushMatrix();
	glScaled(1, -1, 1);
	
	glEnable(GL_DEPTH_TEST);
	if(hasRGBImage){
		currentRGBImage->getTextureReference().bind();
	}
	simpleMesh.drawWireframe();
	if(hasRGBImage){
		currentRGBImage->getTextureReference().unbind();
	}
	glDisable(GL_DEPTH_TEST);
	
	glPopMatrix();	
}

ofTexture& ofxRGBDRenderer::getTextureReference(){
	return currentRGBImage->getTextureReference();
}
