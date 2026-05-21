/* Shared PDF export utility. Requires jsPDF loaded on page. */
(function (global) {
  "use strict";

  function puzzleChars(order) {
    if (order === 3) return "123456789";
    if (order === 4) return "0123456789abcdef";
    return "abcdefghijklmnopqrstuvwxy";
  }

  function normalizePuzzle(puzzle, order) {
    const n = order * order;
    const len = n * n;
    return String(puzzle || "").padEnd(len, ".").slice(0, len);
  }

  function isFixedCellChar(ch, order) {
    if (!ch || ch === ".") return false;
    const c = ch.toLowerCase();
    if (order === 3) return c >= "1" && c <= "9";
    if (order === 4) return (c >= "0" && c <= "9") || (c >= "a" && c <= "f");
    if (order === 5) return c >= "a" && c <= "y";
    return false;
  }

  /**
   * Draws only the grid (lines + digits). Returns bottom edge Y (mm).
   */
  function drawPuzzleBoard(doc, puzzle, order, opts) {
    const o = opts || {};
    const centerX = o.centerX;
    const topY = o.topY;
    const maxBoardWidthMm = o.maxBoardWidthMm || 130;
    const n = order * order;
    const cellMm = maxBoardWidthMm / n;
    const boardMm = cellMm * n;
    const startX = centerX - boardMm / 2;
    const normalized = normalizePuzzle(puzzle, order);
    const fixedMask = normalizePuzzle(o.fixedMask || "", order);
    const highlightFixed = !!o.highlightFixed;
    const chars = puzzleChars(order);

    doc.setLineWidth(0.2);
    for (let i = 0; i <= n; i += 1) {
      const p = startX + i * cellMm;
      doc.line(p, topY, p, topY + boardMm);
      doc.line(startX, topY + i * cellMm, startX + boardMm, topY + i * cellMm);
    }

    doc.setLineWidth(0.7);
    for (let i = 0; i <= n; i += order) {
      const p = startX + i * cellMm;
      doc.line(p, topY, p, topY + boardMm);
      doc.line(startX, topY + i * cellMm, startX + boardMm, topY + i * cellMm);
    }

    const fontSize = Math.max(5, Math.min(12, cellMm * 2.2));
    doc.setFontSize(fontSize);
    for (let i = 0; i < normalized.length; i += 1) {
      const ch = normalized[i].toLowerCase();
      if (ch === ".") continue;
      let glyph = ch;
      /* 16×16: internal chars are 0-9 (values 1-10) and a-f (11-16); show 1-16, not raw 0-9 */
      if (order === 4 && ((ch >= "0" && ch <= "9") || (ch >= "a" && ch <= "f"))) {
        glyph = String(chars.indexOf(ch) + 1);
      } else if (order === 5 && ch >= "a" && ch <= "y") {
        glyph = String(chars.indexOf(ch) + 1);
      }
      const row = Math.floor(i / n);
      const col = i % n;
      const x = startX + col * cellMm + cellMm / 2;
      const y = topY + row * cellMm + cellMm * 0.68;
      const fixed = highlightFixed && isFixedCellChar(fixedMask[i], order);
      doc.setFont("helvetica", fixed ? "bold" : "normal");
      doc.text(glyph, x, y, { align: "center" });
    }

    return topY + boardMm;
  }

  function drawPuzzlePage(doc, title, puzzle, order, options) {
    const opts = options || {};
    doc.setFont("helvetica", "bold");
    doc.setFontSize(16);
    doc.text(title, 105, 16, { align: "center" });
    const size = Math.min(160, Math.floor(520 / (order * order)));
    const boardPx = size * (order * order);
    const maxBoardMm = boardPx / 3.78;
    drawPuzzleBoard(doc, puzzle, order, {
      centerX: 105,
      topY: 28,
      maxBoardWidthMm: maxBoardMm,
      fixedMask: opts.fixedMask || "",
      highlightFixed: !!opts.highlightFixed,
    });
  }

  function downloadPuzzlePdf(initialPuzzle, solvedPuzzle, order, filenamePrefix) {
    if (!global.jspdf || !global.jspdf.jsPDF) {
      throw new Error("jsPDF is not loaded.");
    }
    const jsPDF = global.jspdf.jsPDF;
    const doc = new jsPDF({ unit: "mm", format: "a4", orientation: "portrait" });
    drawPuzzlePage(doc, "Initial Puzzle", initialPuzzle, order);
    doc.addPage();
    drawPuzzlePage(doc, "Solved Puzzle", solvedPuzzle, order, { fixedMask: initialPuzzle, highlightFixed: true });
    const safePrefix = String(filenamePrefix || "puzzle").trim() || "puzzle";
    const name = safePrefix.toLowerCase().endsWith(".pdf") ? safePrefix : (safePrefix + ".pdf");
    doc.save(name);
  }

  /** Puzzle only: size line + centered grid (Experiment / Game download). */
  function downloadPuzzleOnlyLayoutPdf(puzzle, order, filenamePrefix, sizeLabelInner) {
    if (!global.jspdf || !global.jspdf.jsPDF) {
      throw new Error("jsPDF is not loaded.");
    }
    const jsPDF = global.jspdf.jsPDF;
    const doc = new jsPDF({ unit: "mm", format: "a4", orientation: "portrait" });
    const label = String(sizeLabelInner || "").trim() || "—";
    doc.setFont("helvetica", "bold");
    doc.setFontSize(14);
    doc.text("Size of the Puzzle (" + label + ")", 105, 22, { align: "center" });
    const size = Math.min(160, Math.floor(520 / (order * order)));
    const boardPx = size * (order * order);
    const maxBoardMm = boardPx / 3.78;
    drawPuzzleBoard(doc, puzzle, order, {
      centerX: 105,
      topY: 32,
      maxBoardWidthMm: maxBoardMm,
    });
    const safePrefix = String(filenamePrefix || "puzzle").trim() || "puzzle";
    const name = safePrefix.toLowerCase().endsWith(".pdf") ? safePrefix : (safePrefix + ".pdf");
    doc.save(name);
  }

  /** Plain text for PDF (avoid HTML entities and stray markup in labels). */
  function pdfSafeText(s) {
    if (s == null) return "";
    let t = String(s);
    t = t.replace(/&nbsp;/gi, " ");
    t = t.replace(/&amp;/g, "&");
    t = t.replace(/&lt;/g, "<");
    t = t.replace(/&gt;/g, ">");
    t = t.replace(/<[^>]+>/g, "");
    return t.trim();
  }

  /** Page of details only (portrait). */
  function drawDetailsPage(doc, meta) {
    const margin = 16;
    let y = 22;
    const lineH = 5.5;
    const pageBottom = 278;
    const maxTextW = 180;

    doc.setFont("helvetica", "bold");
    doc.setFontSize(14);
    doc.text("Details", margin, y);
    y += 10;

    doc.setFont("helvetica", "normal");
    doc.setFontSize(10);

    function writeLine(label, value) {
      const raw = pdfSafeText(label) + ": " + pdfSafeText(value);
      const lines = doc.splitTextToSize(raw, maxTextW);
      for (let i = 0; i < lines.length; i += 1) {
        if (y + lineH > pageBottom) {
          doc.addPage("a4", "p");
          y = margin + 4;
        }
        doc.text(lines[i], margin, y);
        y += lineH;
      }
    }

    writeLine("Downloaded", meta.downloadedAt || "");
    writeLine("Puzzle Size", meta.puzzleSizeLabel || "");

    const params = meta.solverParams;
    if (params && params.length) {
      y += 3;
      if (y + lineH > pageBottom) {
        doc.addPage("a4", "p");
        y = margin + 4;
      }
      doc.setFont("helvetica", "bold");
      doc.setFontSize(11);
      doc.text("Solver parameters", margin, y);
      y += 8;
      doc.setFont("helvetica", "normal");
      doc.setFontSize(10);
      for (let i = 0; i < params.length; i += 1) {
        const row = params[i];
        writeLine(row.label, row.value);
      }
    }
  }

  /** Initial vs current: page 1 landscape (larger boards); page 2 portrait details. */
  function downloadInitialCurrentComparisonPdf(initialPuzzle, currentPuzzle, order, filenamePrefix, meta) {
    if (!global.jspdf || !global.jspdf.jsPDF) {
      throw new Error("jsPDF is not loaded.");
    }
    const jsPDF = global.jspdf.jsPDF;
    const doc = new jsPDF({ unit: "mm", format: "a4", orientation: "landscape" });
    const pageW = 297;
    const margin = 12;
    const gap = 16;
    const innerW = pageW - 2 * margin;
    const colW = (innerW - gap) / 2;
    const colCenterLeft = margin + colW / 2;
    const colCenterRight = margin + colW + gap + colW / 2;
    const maxBoardMm = global.Math.min(128, colW - 6);

    doc.setFont("helvetica", "bold");
    doc.setFontSize(12);
    doc.text("Initial Puzzle", colCenterLeft, 16, { align: "center" });
    doc.text("Current state of the Puzzle", colCenterRight, 16, { align: "center" });

    drawPuzzleBoard(doc, initialPuzzle, order, {
      centerX: colCenterLeft,
      topY: 22,
      maxBoardWidthMm: maxBoardMm,
    });
    drawPuzzleBoard(doc, currentPuzzle, order, {
      centerX: colCenterRight,
      topY: 22,
      maxBoardWidthMm: maxBoardMm,
      fixedMask: initialPuzzle,
      highlightFixed: true,
    });

    doc.addPage("a4", "p");
    drawDetailsPage(doc, meta || {});

    const safePrefix = String(filenamePrefix || "puzzle").trim() || "puzzle";
    const name = safePrefix.toLowerCase().endsWith(".pdf") ? safePrefix : (safePrefix + ".pdf");
    doc.save(name);
  }

  /** @deprecated Use downloadPuzzleOnlyLayoutPdf or downloadInitialCurrentComparisonPdf */
  function downloadSinglePuzzlePdf(puzzle, order, filenamePrefix, options) {
    if (!global.jspdf || !global.jspdf.jsPDF) {
      throw new Error("jsPDF is not loaded.");
    }
    const jsPDF = global.jspdf.jsPDF;
    const doc = new jsPDF({ unit: "mm", format: "a4", orientation: "portrait" });
    const opts = options || {};
    const title = opts.title || "Puzzle";
    doc.setFont("helvetica", "bold");
    doc.setFontSize(16);
    doc.text(title, 105, 16, { align: "center" });
    const size = Math.min(160, Math.floor(520 / (order * order)));
    const boardPx = size * (order * order);
    const maxBoardMm = boardPx / 3.78;
    drawPuzzleBoard(doc, puzzle, order, {
      centerX: 105,
      topY: 28,
      maxBoardWidthMm: maxBoardMm,
      fixedMask: opts.fixedMask || "",
      highlightFixed: !!opts.highlightFixed,
    });
    const safePrefix = String(filenamePrefix || "puzzle").trim() || "puzzle";
    const name = safePrefix.toLowerCase().endsWith(".pdf") ? safePrefix : (safePrefix + ".pdf");
    doc.save(name);
  }

  global.PdfUtils = {
    downloadPuzzlePdf: downloadPuzzlePdf,
    downloadSinglePuzzlePdf: downloadSinglePuzzlePdf,
    downloadPuzzleOnlyLayoutPdf: downloadPuzzleOnlyLayoutPdf,
    downloadInitialCurrentComparisonPdf: downloadInitialCurrentComparisonPdf,
  };
})(window);
