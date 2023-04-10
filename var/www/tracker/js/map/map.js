/**
 * Elements that make up the popup.
 */
var container = document.getElementById('popup');
var content = document.getElementById('popup-content');
var closer = document.getElementById('popup-closer');


/**
 * Add a click handler to hide the popup.
 * @return {boolean} Don't follow the href.
 */
closer.onclick = function() {
    overlay.setPosition(undefined);
    closer.blur();
    return false;
};

function travelLayerStyle(feature) {
    var styles = [
        // linestring
        new ol.style.Style({
            stroke: new ol.style.Stroke({
                color: 'blue',
                width: 4,
            }),
        })
    ];

    feature.getGeometry().forEachSegment(function(start, end) {
        var dx = end[0] - start[0];
        var dy = end[1] - start[1];
        var rotation = Math.atan2(dy, dx);
        // arrows
        styles.push(
            new ol.style.Style({
                geometry: new ol.geom.Point(end),
                image: new ol.style.Icon({
                    src: 'icons/arrow.png',
                    anchor: [0.75, 0.5],
                    rotateWithView: true,
                    rotation: -rotation,
                }),
            })
        );
    });

    return styles;
}

const markerStyle = new ol.style.Style({
    image: new ol.style.Icon({
        anchor: [0.5, 1],
        size: [400, 600],
        offset: [0, 0],
        opacity: 1,
        scale: 0.12,
        src: "pin.png"
    })
});



var overlay = new ol.Overlay({
    element: container,
    autoPan: true,
    autoPanAnimation: {
        duration: 250
    }
});

var interactions = ol.interaction.defaults.defaults({
    altShiftDragRotate: false,
    pinchRotate: false,
    keyboard: true,
    mouseWheelZoom: true
});

var map = new ol.Map({
    target: 'map',
    view: new ol.View({
        center: defaultCenter,
        zoom: 17,
        maxZoom: 20
    }),
    overlays: [overlay],
    renderer: 'webgl',
    interactions: interactions,
    controls: []
});

var satLayer = new ol.layer.Tile({
    title: "Google Satellite",
    source: new ol.source.TileImage({
        maxZoom: 19,
        wrapX: true,
        url: 'https://mt1.google.com/vt/lyrs=s&hl=pl&&x={x}&y={y}&z={z}'
    }),
    visible: false
});

map.addLayer(satLayer);

var osmLayer = new ol.layer.Tile({
    title: "Open street maps",
    visible: true,
    source: new ol.source.OSM(),
    minZoom: 19 // visible at zoom levels 14 and below
})

map.addLayer(osmLayer);

var arcgisLayer = new ol.layer.Tile({
    title: "ArcGIS map",
    visible: true,
    source: new ol.source.XYZ({
        url: 'https://server.arcgisonline.com/ArcGIS/rest/services/World_Topo_Map/MapServer/tile/{z}/{y}/{x}',
    }),
    maxZoom: 19 // visible at zoom levels above 14
});

map.addLayer(arcgisLayer); //use arcGIS instead of open street map - it's much faster

var travelFeature = new ol.Feature({
    geometry: new ol.geom.LineString([])
});

var travelLayer = new ol.layer.VectorImage({
    title: "Route history",
    source: new ol.source.Vector({
        features: [
            travelFeature
        ]
    }),
    style: travelLayerStyle(travelFeature),
    renderMode: 'image'
});


map.addLayer(travelLayer);


var pointFeature = new ol.Feature(new ol.geom.Point(defaultCenter));
var currentPointLayer = new ol.layer.Vector({
    title: "Current location",
    source: new ol.source.Vector({
        features: [
            pointFeature
        ]
    }),
    style: markerStyle
});

map.addLayer(currentPointLayer);


function eventStyle(feature) {
    var c1 = 0;
    for (let i = 0; i < feature.EVT.length; i++) {
        c1 += feature.EVT.charCodeAt(i);
    }

    var fillcolor = 'rgba(' + (Math.floor(c1) % 10) * 25 + ', ' + (Math.floor(c1 / 100) % 10) * 25 + ', ' + (Math.floor(c1 / 10) % 10) * 25 + ', 1)';

    if (feature.EVT.includes("outside of inclusion zone")) {
        fillcolor = 'rgba(255,0,0,1)';
    }

    if (feature.EVT.includes("entered fence area")) {
        fillcolor = 'rgba(0,255,0,1)';
    }

    if (feature.EVT.includes("left fence area")) {
        fillcolor = 'rgba(0,191,255,1)';
    }

    if (feature.EVT.includes("inside exclusion zone")) {
        fillcolor = 'rgba(178,34,34,1)';
    }

    return [
        new ol.style.Style({
            stroke: new ol.style.Stroke({
                color: fillcolor,
                width: 3
            }),
            fill: new ol.style.Fill({
                color: fillcolor
            })
        })
    ];
}


var eventLayer = new ol.layer.Vector({
    source: new ol.source.Vector({
        projection: 'EPSG:4326',
        features: []
    }),
    style: eventStyle
});

map.addLayer(eventLayer);




function geofenceStyle(feature) {
    var color = 'green';
    var fillcolor = 'rgba(0, 255, 0, 0.1)';
    switch (feature.TYPE) {
        case "0":
            color = 'pink';
            fillcolor = 'rgba(255, 0, 255, 0.1)';
            break;
        case "1":
            color = 'yellow';
            fillcolor = 'rgba(255, 255, 0, 0.1)';
            break;
        case "2":
            color = 'blue';
            fillcolor = 'rgba(0, 0, 255, 0.1)';
            break;
        case "3":
            color = 'green';
            fillcolor = 'rgba(0, 255, 0, 0.1)';
            break;
        case "demo":
            color = 'gray';
            fillcolor = 'rgba(128, 128, 128, 0.1)';
            break;
        case "4":
        default:
            color = 'red';
            fillcolor = 'rgba(255, 0, 0, 0.1)';
            break;

    }

    return [
        new ol.style.Style({
            stroke: new ol.style.Stroke({
                color: color,
                width: 3
            }),
            fill: new ol.style.Fill({
                color: fillcolor
            })
        })
    ];
}


var geofenceLayer = new ol.layer.Vector({
    source: new ol.source.Vector({
        projection: 'EPSG:4326',
        features: []
    }),
    style: geofenceStyle
});

map.addLayer(geofenceLayer);