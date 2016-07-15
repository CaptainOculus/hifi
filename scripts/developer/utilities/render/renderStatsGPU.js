//
//  renderStats.js
//  examples/utilities/tools/render
//
//  Sam Gateau, created on 3/22/2016.
//  Copyright 2016 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

// Set up the qml ui
var qml = Script.resolvePath('statsGPU.qml');
var window = new OverlayWindow({
    title: 'Render Stats GPU',
    source: qml,
    width: 320, 
    height: 200
});
window.setPosition(50, 20);
window.closed.connect(function() { Script.stop(); });