//
//  recordingMaster.js
//  examples/entityScripts
//
//  Created by Alessandro Signa on 11/12/15.
//  Copyright 2015 High Fidelity, Inc.
//
//  Run this script to spawn a box (recorder) and drive the start/end of the recording for anyone who is inside the box
//  
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html

HIFI_PUBLIC_BUCKET = "http://s3.amazonaws.com/hifi-public/";
Script.include(HIFI_PUBLIC_BUCKET + "scripts/libraries/toolBars.js");
Script.include(HIFI_PUBLIC_BUCKET + "scripts/libraries/utils.js");

var rotation = Quat.safeEulerAngles(Camera.getOrientation());
rotation = Quat.fromPitchYawRollDegrees(0, rotation.y, 0);
var center = Vec3.sum(MyAvatar.position, Vec3.multiply(1, Quat.getFront(rotation)));

var TOOL_ICON_URL = HIFI_PUBLIC_BUCKET + "images/tools/";
var ALPHA_ON = 1.0;
var ALPHA_OFF = 0.7;
var COLOR_TOOL_BAR = { red: 0, green: 0, blue: 0 };

var toolBar = null;
var recordIcon;
var isRecording = false;
var channel = "groupRecordingChannel";
Messages.subscribe(channel);
setupToolBar();

function setupToolBar() {
    if (toolBar != null) {
        print("Multiple calls to setupToolBar()");
        return;
    }
    Tool.IMAGE_HEIGHT /= 2;
    Tool.IMAGE_WIDTH /= 2;
    toolBar = new ToolBar(0, 100, ToolBar.HORIZONTAL);    //put the button in the up-left corner
    toolBar.setBack(COLOR_TOOL_BAR, ALPHA_OFF);
    
    recordIcon = toolBar.addTool({
        imageURL: TOOL_ICON_URL + "recording-record.svg",
        subImage: { x: 0, y: 0, width: Tool.IMAGE_WIDTH, height: Tool.IMAGE_HEIGHT },
        x: 0, y: 0,
        width: Tool.IMAGE_WIDTH,
        height: Tool.IMAGE_HEIGHT,
        alpha: Recording.isPlaying() ? ALPHA_OFF : ALPHA_ON,
        visible: true,
    }, true, isRecording);
}

function mousePressEvent(event) {
    clickedOverlay = Overlays.getOverlayAtPoint({ x: event.x, y: event.y });
    if (recordIcon === toolBar.clicked(clickedOverlay, false)) {
        if (!isRecording) {
            print("I'm the master. I want to start recording");
            var message = "RECONDING STARTED";
            Messages.sendMessage(channel, message);
            isRecording = true;
        } else {
            print("I want to stop recording");
            var message = "RECONDING ENDED";
            Messages.sendMessage(channel, message);
            isRecording = false;
        }
    }
}

function cleanup() {
    toolBar.cleanup();
    Messages.unsubscribe(channel);
}

Script.scriptEnding.connect(cleanup);
Controller.mousePressEvent.connect(mousePressEvent);
