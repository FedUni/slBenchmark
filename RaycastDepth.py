import bpy
import math
import sys
import os
import json
import numpy as np
from mathutils import Vector

def getCameraDirection():
	Y = np.tan(bpy.data.cameras['Camera.001'].angle_x / 2)
	Z = np.tan(bpy.data.cameras['Camera.001'].angle_y / 2)

	return (-1, Y, Z)

def getProjectorDirection():
	Y = np.tan(bpy.data.lamps['Spot'].spot_size / 2)
	Z = np.tan(bpy.data.cameras['Camera.001'].angle_y / 2)

	#return (-1, Y, Y)
	return (-1, Y, Z)

def getDirections(obj, source, sourceWidth, sourceHeight, outputPath):
	out = open(outputPath, 'w')
	outReal = open(outputPath + ".real.xyz", 'w')
	#x, y, z = getDirection()
	x, y, z = getProjectorDirection()
	#zPixel = sourceWidth/(2*y)  # This the depth of the picture.

	for yDirection in np.linspace(-y, y, sourceWidth, endpoint=False):
		for zDirection in np.linspace(z, -z, sourceHeight, endpoint=False):
			dst = Vector((x, yDirection, zDirection)) + source.location

			mw = obj.matrix_world
			mwi = mw.inverted()

			# src and dst in local space of cb

			origin = mwi * source.location
			dest = mwi * dst
			direction = (dest - origin).normalized()

			result, location, normal, index = obj.ray_cast(origin,  direction)

			xPixel = int(round(((yDirection + y) / (2 * y)) * sourceWidth))
			yPixel = int(round(((-zDirection + z) / (2 * z)) * sourceHeight))
			locationWorld = mw * location
			fromSource = locationWorld - source.location

			if result:
				zReal = fromSource.x # This gives the depth of the pixel under consideration
				xReal = (xPixel-sourceWidth/2)*zReal*(2*y/sourceWidth)
				yReal = (yPixel-sourceHeight/2)*zReal*(2*z/sourceHeight)

#				if xPixel == 900 and yPixel == 312:
#					print("xReal[" + str(xReal) + "] = (xPixel[" + str(xPixel) + "]-sourceWidth[" + str(sourceWidth) + "]/2)*zReal[" + str(zReal) + "]*(2*y[" + str(y) + "]/sourceWidth[" + str(sourceWidth) + "]")
#					print("yReal[" + str(yReal) + "] = (yPixel[" + str(yPixel) + "]-sourceHeight[" + str(sourceHeight) + "]/2)*zReal[" + str(zReal) + "]*(2*z[" + str(z) + "]/sourceHeight[" + str(sourceHeight) + "]")
			
				outReal.write(str(xReal) + " " + str(yReal) + " " + str(zReal) + '\n')
				out.write(str(xPixel) + " " + str(yPixel) + " " + str(zReal) + '\n')
				#out.write(str(fromCamera.y) + " " + str(fromCamera.z) + " " + str(fromCamera.x) + '\n')
	out.close()

if __name__ == "__main__":
	argv = sys.argv
	argv = argv[argv.index("--") + 1:] 

	bpy.ops.wm.open_mainfile(filepath=os.path.abspath(argv[0]))

	outputPath = os.path.abspath(argv[1])
	sourceWidth = int(argv[2])
	sourceHeight = int(argv[3])

	singleObject = None
	numMeshObjects = 0

	for ob in bpy.context.scene.objects:
		if ob.type == 'MESH':
			ob.select = True
			bpy.context.scene.objects.active = ob			
			numMeshObjects = numMeshObjects + 1
		else:
			ob.select = False

	if numMeshObjects > 1:
		bpy.ops.object.join()

	for ob in bpy.context.scene.objects:
		if ob.type == 'MESH':
			singleObject = ob

	bpy.context.scene.update()

	#getDirections(singleObject, bpy.context.scene.camera, sourceWidth, sourceHeight, outputPath)
	getDirections(singleObject, bpy.context.scene.objects['Spot'], sourceWidth, sourceHeight, outputPath)

	
