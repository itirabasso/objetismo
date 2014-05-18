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
cache = {}


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

def RGB_to_XYZ(rgb):
    
    r = rgb[0] / 255.0
    g = rgb[1] / 255.0
    b = rgb[2] / 255.0
    
    if r > 0.04045:
        r = math.pow((r + 0.055) / 1.055, 2.4)
    else:
        r = r / 12.92
    if g > 0.04045:
        g = math.pow((g + 0.055) / 1.055, 2.4)
    else:
        g = g / 12.92
    if b > 0.04045:
        b = math.pow((b) / 1.055, 2.4)
    else:
        b = b / 12.92
      
    return (r, g, b)

def XYZ_to_Lab(xyz):

    x = xyz[0]/  0.95047
    y = xyz[1] / 1.00000
    z = xyz[2] / 1.08883
    
    CIE_E = 0.008856 #216.0/24389.0
    
    if x > CIE_E:
        x = math.pow(x, (1.0 / 3.0))
    else:
        x = (7.787 * x) + (16.0 / 116.0)     

    if y > CIE_E:
        y = math.pow(y, (1.0 / 3.0))
    else:
        y = (7.787 * y) + (16.0 / 116.0)
   
    if z > CIE_E:
        z = math.pow(z, (1.0 / 3.0))
    else:
        z = (7.787 * z) + (16.0 / 116.0)
      
    l = (116.0 * y) - 16.0
    a = 500.0 * (x - y)
    b = 200.0 * (y - z)

    return (l,a,b)

def cie1976(color1, color2):
    r1,g1,b1,alpha1 = color1
    r2,g2,b2,alpha2 = color2
    lab1 = XYZ_to_Lab(RGB_to_XYZ((r1,g1,b1)))
    lab2 = XYZ_to_Lab(RGB_to_XYZ((r2,g2,b2)))
    
    return math.sqrt((lab1[0]-lab2[0])**2 + (lab1[1]-lab2[1])**2 + (lab1[2]-lab2[2])**2)

def cie1994(color1, color2):
    r1,g1,b1,alpha1 = color1
    r2,g2,b2,alpha2 = color2

    l1,a1,b1 = XYZ_to_Lab(RGB_to_XYZ((r1,g1,b1)))
    l2,a2,b2 = XYZ_to_Lab(RGB_to_XYZ((r2,g2,b2)))

    dL = l1 - l2
    dA = a1 - a2
    dB = b1 - b2
    c1 = math.sqrt(a1**2 + b1**2)
    c2 = math.sqrt(a2**2 + b2**2)
    dC = c1 - c2
    dH2 = dA**2 + dB**2 - dC**2
    if dH2 > 0.0:
        dH = math.sqrt(dH2)
    else:
        dH = 0.0
    sL = 1.0
    k1 = 0.045 #0.048
    k2 = 0.015 #0.014
    sC = 1.0 + k1 * c1
    sH = 1.0 + k2 * c1
    
    kL = 1.0
    kC = 1.0
    kH = 1.0
    
    e = (dL / (kL * sL))**2 + (dC / (kC * sC))**2 + (dH / (kH * sH))**2
    
    return math.sqrt(e)

def cie2000(color1,color2):
    r1,g1,b1,alpha1 = color1
    r2,g2,b2,alpha2 = color2

    l1,a1,b1 = XYZ_to_Lab(RGB_to_XYZ((r1,g1,b1)))
    l2,a2,b2 = XYZ_to_Lab(RGB_to_XYZ((r2,g2,b2)))
    
    L = 1.0 # 2
    C = 1.0 
    
    c1 = math.sqrt(a1**2 + b1**2)
    c2 = math.sqrt(a2**2 + b2**2)
    if l1 < 16.0:
        sL = 0.511 
    else:
        sL = (0.040975 * l1) / (1.0 + 0.01765 * l1)
    sC = (0.0638 * c1) / (1.0 + 0.0131 * c1) + 0.638;
    if c1 < 0.000001:
        h1 = 0.0
    else:
        h1 = (math.atan2(b1, a1) * 180.0) / math.pi
        
    while (h1 < 0.0):
        h1 += 360.0
    while (h1 >= 360.0):
        h1 -= 360.0;
        
    if (h1 >= 164.0) and (h1 <= 345.0):
        t = (0.56 + abs(0.2 * math.cos((math.pi * (h1 + 168.0)) / 180.0)))
    else: 
        t = (0.36 + abs(0.4 * math.cos((math.pi * (h1 + 35.0)) / 180.0)))
        
    c4 = c1**4
    
    f = math.sqrt(c4 / (c4 + 1900.0));
    sH = sC * (f * t + 1.0 - f)
    dL = l1 - l2;
    dC = c1 - c2;
    dA = a1 - a2
    dB = b1 - b2
    dH2 = dA**2 + dB**2 - dC**2
    v1 = dL / (L * sL);
    v2 = dC / (C * sC);
    v3 = sH;
    
    if (L == 1.0):
        return math.sqrt(v1 * v1 + v2 * v2 + (dH2 / (v3 * v3)));
    else:
        return math.sqrt(v1 * v1 + v2 * v2 + (dH2 / (v3 * v3)));

def colorDistance(color1, color2):

    return algorithm(color1, color2)



def getObjectForColor(color):
    colorValue = getColor(color)
    try:
        r = cache[colorValue]
        # print(color)
        return r
    except:
        d = 9223372036854775807 
        ret = None
        for c in colors:
            cD = colorDistance(color, colors[c])
            
            # Same color
            if cD == 0:
                ret = c
                break;

            if cD < d:
                d = cD
                ret = c

    cache[colorValue] = loadObjectGraphic(ret)
    return cache[colorValue] 
        
        
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
parser.add_argument('--image', '-i', required=True)
parser.add_argument('--algo', '-a', default=3, type=int, help='Opcional. Hay 6 algoritmos: CIE 1976 (1), CIE 1994 (2)')
parser.add_argument('--tolerance', '-t', default=30)
parser.add_argument('--graphics', '-g', default='resources/graficos/')
parser.add_argument('--dats', '-d', default='resources/obj.dat')
parser.add_argument('--index', '-n', default='resources/Graficos3.ind')
parser.add_argument('--output', '-o', default='output/')

# 
args = parser.parse_args()
filename = args.image
tolerance = args.tolerance
graphics = args.graphics
dats = args.dats
index = args.index
if args.algo == 1:
    algorithm = cie1976
elif args.algo == 2:
    algorithm = cie1994
elif args.algo == 3:
    algorithm = cie2000


pygame.init()

loadGraphicsData(index)
loadObjects(dats)
loadGraphics(tolerance)

image = pygame.image.load(filename)

w,h = image.get_size()

s = pygame.Surface((w*32, h*32))

for y in xrange(h):
    print str(y+1) + " / " + str(h)
    for x in xrange(w):
        obj = getObjectForColor(image.get_at((x,y)))
        s.blit(obj, (x*32, y*32))
        
    
now = datetime.datetime.now()
outputFile = 'output/' + now.strftime("%Y-%m-%d %H:%M:%S") + '.png'
print 'Guardando imagen en ' + outputFile
saveSurface(s, outputFile)
