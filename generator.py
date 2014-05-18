#!/usr/bin/python3
# -*- coding: utf-8 -*- 

import pygame
import datetime
import sys
import struct
import math
import ConfigParser
import argparse


surfaces = {}
objects = {}
colors = {}

def getAverageColor(surface):
    r, g, b = 0, 0, 0
    count = 0
    for x in xrange(surface.get_width()):
        for y in xrange(surface.get_height()):
            cR, cG, cB, alpha = surface.get_at((x, y))
            if cR+cG+cB == 0: continue
         
            r += cR
            g += cG
            b += cB
            count += 1
    if (count == 0): 
        return (0,0,0,1)
    return (r/count), (g/count), (b/count), count+1


def saveSurface(surface, filename):
    pygame.image.save(surface, filename)

def getColor(color):
    r = color[0]
    g = color[1]
    b = color[2]
    return r + g*256 + b*65536 

def loadObjectGraphic(objectId):
    if objectId == None: return None
    grh = int(objects[objectId]['graphic'])
    grhData = surfaces[grh]
    objectSurface = pygame.Surface((32, 32))
    objectSurface.blit(
        pygame.image.load(graphics + str(grhData['file']) + '.bmp'),
        (0, 0, 32, 32), 
        (grhData['sX'], grhData['sY'], grhData['sX'] + 32, grhData['sY'] + 32)
    )
    return objectSurface

def loadGraphics(tolerance):
    for object in objects:
        if object == None or objects[object] == None: continue
        
        grh = int(objects[object]['graphic'])

        if (grh == None or grh <= 0 or surfaces[grh] == None): continue
        
        s = loadObjectGraphic(object)
        color = getAverageColor(s)
    
        # Descarto los objetos que tienen mucho negro (ejemplo: llaves)
        if color[3] > 32*32*tolerance/100.0: 
            colors[object] = color

def loadObjects(dats):
    config = ConfigParser.ConfigParser()
    config.read(dats)
    maxObjects = int(config.get('INIT', 'NumOBJs'))
    for i in xrange(1, maxObjects):
        sectionName = 'OBJ' + str(i)
        if not config.has_section(sectionName):
            objects[i] = None
        else:
            objects[i] = {
                'name': config.get(sectionName, 'Name'),
                'graphic': config.get(sectionName, 'GrhIndex') 
            }

def writeObjectsColors():
    f = open("resources/colors", 'wb')
    for i in colors:
        c = colors[i]
        #soy negro, si senior.
        # print str(i) + " => (" + str(c[0]) + "," + str(c[1]) + "," + str(c[2]) + ")"
        f.write(struct.pack('H', i))
        f.write(chr(c[0])) 
        f.write(chr(c[1]))
        f.write(chr(c[2]))

    f.close()

def loadGraphicsData(indexesFile):
    
    file = open(indexesFile, 'r')
    
    struct.unpack('i', file.read(4))[0]
    
    grhCount = struct.unpack('i', file.read(4))[0]
    grh = 0
    data = None
    while grhCount-1 != grh:
        grh = struct.unpack('i', file.read(4))[0]
        if grh != 0:
            framesAmount = struct.unpack('h', file.read(2))[0]
            if framesAmount <= 0:  return

            # animation
            if framesAmount > 1:
                for i in range(framesAmount):
                    frame = struct.unpack('i', file.read(4))[0]
                    if frame <= 0 or frame > grhCount: return;                        
                
                speed = struct.unpack('i', file.read(4))
                if (speed <= 0): return
                
                data = None
            else:
                fileNumber = struct.unpack('i', file.read(4))[0]
                if fileNumber <= 0: return
                
                sX = struct.unpack('h', file.read(2))[0]
                if sX < 0: return
                
                sY = struct.unpack('h', file.read(2))[0]
                if sY < 0: return
                
                tileWidth = struct.unpack('h', file.read(2))[0]
                if tileWidth <= 0: return
                
                tileHeight = struct.unpack('h', file.read(2))[0]
                if tileHeight <= 0: return
                
                # exclude non tile-size graphics.
                if tileHeight == 32 and tileWidth == 32:
                    data = {
                        'file': fileNumber,
                        'sX': sX,
                        'sY': sY,
                        'width': tileWidth,
                        'height': tileHeight
                    }
                else:
                    data = None

            surfaces[grh] = data


parser = argparse.ArgumentParser(description='Objetismo')
parser.add_argument('--image', '-i', default="matrix")
parser.add_argument('--tolerance', '-t', default=30)
parser.add_argument('--graphics', '-g', default='resources/graficos/')
parser.add_argument('--dats', '-d', default='resources/obj.dat')
parser.add_argument('--index', '-n', default='resources/Graficos3.ind')
parser.add_argument('--output', '-o', default='output/')


args = parser.parse_args()
graphics = args.graphics

pygame.init()

loadGraphicsData(args.index)
loadObjects(args.dats)
loadGraphics(args.tolerance)

file = open(args.image, 'r')

# negro
w = struct.unpack('h', file.read(2))[0]
h = struct.unpack('h', file.read(2))[0]
s = pygame.Surface((w*32, h*32))
for x in xrange(w):
    print str(x+1) + " / " + str(w)
    for y in xrange(h):
        objId = struct.unpack('h', file.read(2))[0]
        obj = loadObjectGraphic(objId)
        s.blit(obj, (x*32, y*32))

    
now = datetime.datetime.now()
outputFile = 'output/' + now.strftime("%Y-%m-%d %H:%M:%S") + '.png'
print 'Guardando imagen en ' + outputFile
saveSurface(s, outputFile)
