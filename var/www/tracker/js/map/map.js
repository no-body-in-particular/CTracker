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

//How far apart the direction arrows sit, measured on the screen rather than on the ground.
var ARROW_SPACING_PX = 90;

//A ceiling on how many are drawn at once. Only reachable when a track wanders far outside
//the viewport, since anything on screen is bounded by the window divided by the spacing.
var ARROW_LIMIT = 400;

/*
 * The travelled track, with an arrow every ARROW_SPACING_PX along it.
 *
 * There used to be one arrow per segment. A segment is one reading to the next, so at a
 * minute apart and walking pace they are metres long: zoom out and several hundred fixed
 * size arrows land on top of each other and the track reads as a thick worm rather than a
 * line with a direction.
 *
 * Spacing them by distance on screen fixes it at every zoom, and it is the spacing that
 * has to change rather than the size - shrinking the arrows to fit would just make an
 * unreadable worm out of smaller arrows.
 *
 * Returned as a function, which is what makes it work: OpenLayers calls a style function
 * again on every render with the current resolution, while a style array is built once and
 * never revisited. The old one kept whatever spacing was current when the track was first
 * drawn, however far you zoomed afterwards. Every caller already passes the result straight
 * to setStyle() or a layer's style, both of which take a function, so none of them change.
 */
function travelLayerStyle(feature) {
    return function(styled, resolution) {
        var styles = [
            // linestring
            new ol.style.Style({
                stroke: new ol.style.Stroke({
                    color: 'blue',
                    width: 4,
                }),
            })
        ];

        var geometry = styled ? styled.getGeometry() : feature.getGeometry();

        if (!geometry || !resolution) {
            return styles;
        }

        //resolution is map units per pixel, so this is the on-screen spacing in map units
        var spacing = ARROW_SPACING_PX * resolution;
        //distance already walked since the last arrow, carried across segment boundaries so
        //that a run of very short segments still accumulates into one arrow
        var since = spacing / 2;

        geometry.forEachSegment(function(start, end) {
            if (styles.length > ARROW_LIMIT) {
                return;
            }

            var dx = end[0] - start[0];
            var dy = end[1] - start[1];
            var length = Math.sqrt(dx * dx + dy * dy);

            if (length === 0) {
                return;
            }

            var rotation = Math.atan2(dy, dx);

            for (var along = spacing - since; along <= length; along += spacing) {
                var t = along / length;

                styles.push(
                    new ol.style.Style({
                        geometry: new ol.geom.Point([start[0] + dx * t, start[1] + dy * t]),
                        image: new ol.style.Icon({
                            src: 'icons/arrow.png',
                            anchor: [0.75, 0.5],
                            rotateWithView: true,
                            rotation: -rotation,
                        }),
                    })
                );

                if (styles.length > ARROW_LIMIT) {
                    break;
                }
            }

            since = (since + length) % spacing;
        });

        return styles;
    };
}

const markerIcon = new ol.style.Icon({
    anchor: [0.5, 1],
    size: [400, 600],
    offset: [0, 0],
    opacity: 1,
    scale: 0.12,
    src: "pin.png"
});

const markerStyle = new ol.style.Style({
    image: markerIcon
});

/*
 * The pin with its readings written under it, one to a line. The icon is anchored at its foot,
 * so the point is at the bottom of the pin and the text hangs below it - baseline at the top so
 * it grows downwards and the first line stays put however many there are. Drawn with a dark
 * outline because it has to stay readable over map tiles of any colour.
 */
function markerStyleWithLabel(lines) {
    var text = (lines || []).filter(Boolean).join('\n');

    if (!text) {
        return markerStyle;
    }

    return new ol.style.Style({
        image: markerIcon,
        text: new ol.style.Text({
            text: text,
            font: 'bold 12px Montserrat, sans-serif',
            textBaseline: 'top',
            offsetY: 8,
            fill: new ol.style.Fill({ color: '#ffffff' }),
            stroke: new ol.style.Stroke({ color: 'rgba(0, 0, 0, 0.8)', width: 3 })
        })
    });
}



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