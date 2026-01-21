/******************* GLOBAL CONFIG *******************/
var SPREADSHEET_ID = '1S1thCQA27F1jXXrOXHawQkU1u7ONVUFHT6t9iyF_Td0';
var timezone = "Asia/Kolkata";

/******************* DO GET (ATTENDANCE) *******************/
function doGet(e) {
  try {
    var ss = SpreadsheetApp.openById(SPREADSHEET_ID);
    var sheet = ss.getSheetByName('Sheet1');

    // ---------- CHECK PARAM ----------
    if (!e || !e.parameters || !e.parameters.data) {
      return ContentService.createTextOutput("Missing data parameter");
    }

    // ---------- SPLIT INCOMING DATA ----------
    // Example: Srinivas 250850330077 DESD
    var parts = e.parameters.data.toString().trim().split(" ");

    if (parts.length < 3) {
      return ContentService.createTextOutput("Invalid data format");
    }

    var name   = parts[0];
    var regNo  = parts[1];
    var course = parts[2];

    // ---------- TODAY DATE ----------
    var todayStr = Utilities.formatDate(
      new Date(),
      timezone,
      "dd/MM/yyyy"
    );

    // ---------- FIND DATE COLUMN ----------
    var START_COL = 5; // Column E
    var DATE_ROW = 7;

    var dateRow = sheet
      .getRange(DATE_ROW, START_COL, 1,
        sheet.getLastColumn() - START_COL + 1)
      .getValues()[0];

    var dateCol = -1;

    for (var i = 0; i < dateRow.length; i++) {
      if (!dateRow[i]) continue;

      if (dateRow[i].toString().trim() === todayStr) {
        dateCol = START_COL + i;
        break;
      }
    }

    if (dateCol === -1) {
      return ContentService.createTextOutput("Date not found in sheet");
    }

    // ---------- FIND STUDENT ROW ----------
    var START_ROW = 8;
    var REG_COL = 2; // Column B

    var regData = sheet
      .getRange(START_ROW, REG_COL,
        sheet.getLastRow() - START_ROW + 1, 1)
      .getValues();

    var studentRow = -1;

    for (var j = 0; j < regData.length; j++) {
      if (!regData[j][0]) continue;

      if (regData[j][0].toString().trim() === regNo.trim()) {
        studentRow = START_ROW + j;
        break;
      }
    }

    if (studentRow === -1) {
      return ContentService.createTextOutput("Student ID not found");
    }

    // ---------- MARK PRESENT ----------
    var cell = sheet.getRange(studentRow, dateCol);
    cell.setValue("P");
    cell.setNote(course);
    cell.setBackground("#006400"); // dark green
    cell.setFontColor("#FFFFFF");

    // ---------- LOG TO SHEET2 ----------
    logToSheet2(name, regNo, course, "P");

    return ContentService.createTextOutput(
      "Attendance marked for ID " + regNo + " on " + todayStr
    );

  } catch (err) {
    return ContentService.createTextOutput("Error: " + err.message);
  }
}

/******************* LOGGING FUNCTION (SHEET2) *******************/
function logToSheet2(name, regNo, course, status) {
  var ss = SpreadsheetApp.openById(SPREADSHEET_ID);
  var sheet2 = ss.getSheetByName("Sheet2");

  sheet2.appendRow([
    new Date(),
    name,
    regNo,
    course,
    status
  ]);
}

/******************* DO POST (JSON API – OPTIONAL) *******************/
function doPost(e) {
  try {
    var sheet2 = SpreadsheetApp
      .openById(SPREADSHEET_ID)
      .getSheetByName("Sheet2");

    var data = JSON.parse(e.postData.contents);

    sheet2.appendRow([
      new Date(),
      data.name || "",
      data.roll || "",
      data.course || "",
      data.uid || ""
    ]);

    return ContentService.createTextOutput("OK");

  } catch (err) {
    return ContentService.createTextOutput("Error: " + err.message);
  }
}
