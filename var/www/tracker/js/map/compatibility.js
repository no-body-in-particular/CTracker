$(document).ready(function() {
    if ($('#beginDate').prop('type') != 'date') $('#beginDate').datepicker();
    var tpSettings = {
        timeFormat: 'HH:mm:ss',
        minHour: 0,
        minMinutes: 0,
        maxHour: 23,
        maxMinutes: 59,
        startTime: new Date(0, 0, 0, 12, 0, 0),
        interval: 15,
        dropdown: true,
        dynamic: true,
        scrollbar: true,
        change: searchdateChange
    };
    if ($('#beginTime').prop('type') != 'time') $('#beginTime').timepicker(tpSettings);
});