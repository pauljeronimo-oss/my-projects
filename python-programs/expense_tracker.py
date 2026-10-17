#!/usr/bin/env python3
"""Expense Tracker - record and analyze expenses from the command line.

Run:  python3 expense_tracker.py
Data is saved automatically to the expenses.json file.
"""

from __future__ import annotations

import csv
import json
import random
from collections import defaultdict
from dataclasses import asdict, dataclass
from datetime import date, datetime, timedelta
from pathlib import Path

DATA_FILE = Path("expenses.json")
CATEGORIES = ["food", "transport", "rent", "bills", "fun", "health", "other"]


# ---------------------------------------------------------------
# Data model
# ---------------------------------------------------------------
@dataclass
class Expense:
    id: int
    day: str  # date in YYYY-MM-DD format
    category: str
    amount: float
    note: str = ""

    @property
    def month(self) -> str:
        return self.day[:7]  # YYYY-MM


class ExpenseTracker:
    """Keeps track of expenses and monthly budgets by category."""

    def __init__(self, path: Path = DATA_FILE) -> None:
        self.path = path
        self.expenses: list[Expense] = []
        self.budgets: dict[str, float] = {}
        self._load()

    # ---------- save / load ----------
    def _load(self) -> None:
        if not self.path.exists():
            return
        try:
            raw = json.loads(self.path.read_text(encoding="utf-8"))
            self.expenses = [Expense(**item) for item in raw.get("expenses", [])]
            self.budgets = {k: float(v) for k, v in raw.get("budgets", {}).items()}
        except (json.JSONDecodeError, TypeError, ValueError) as exc:
            print(f"Warning: could not read {self.path} ({exc}). Starting empty.")

    def _save(self) -> None:
        payload = {
            "expenses": [asdict(e) for e in self.expenses],
            "budgets": self.budgets,
        }
        self.path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")

    # ---------- operations ----------
    def _next_id(self) -> int:
        return max((e.id for e in self.expenses), default=0) + 1

    def add(self, day: str, category: str, amount: float, note: str = "") -> Expense:
        expense = Expense(self._next_id(), day, category, round(amount, 2), note)
        self.expenses.append(expense)
        self._save()
        return expense

    def delete(self, expense_id: int) -> bool:
        before = len(self.expenses)
        self.expenses = [e for e in self.expenses if e.id != expense_id]
        if len(self.expenses) == before:
            return False
        self._save()
        return True

    def filter(self, category: str | None = None, month: str | None = None) -> list[Expense]:
        result = self.expenses
        if category:
            result = [e for e in result if e.category == category]
        if month:
            result = [e for e in result if e.month == month]
        return sorted(result, key=lambda e: (e.day, e.id))

    def totals_by_category(self, month: str | None = None) -> dict[str, float]:
        totals: dict[str, float] = defaultdict(float)
        for e in self.filter(month=month):
            totals[e.category] += e.amount
        return dict(sorted(totals.items(), key=lambda kv: kv[1], reverse=True))

    def totals_by_month(self) -> dict[str, float]:
        totals: dict[str, float] = defaultdict(float)
        for e in self.expenses:
            totals[e.month] += e.amount
        return dict(sorted(totals.items()))

    def set_budget(self, category: str, amount: float) -> None:
        if amount <= 0:
            self.budgets.pop(category, None)  # 0 = delete budget
        else:
            self.budgets[category] = amount
        self._save()

    def budget_report(self, month: str) -> list[tuple[str, float, float]]:
        """Returns a list of (category, spent, limit) tuples for the given month."""
        spent = self.totals_by_category(month)
        return [(cat, spent.get(cat, 0.0), limit) for cat, limit in sorted(self.budgets.items())]

    def export_csv(self, path: Path) -> int:
        with path.open("w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            writer.writerow(["id", "date", "category", "amount", "note"])
            for e in self.filter():
                writer.writerow([e.id, e.day, e.category, f"{e.amount:.2f}", e.note])
        return len(self.expenses)

    def add_sample_data(self, count: int = 25) -> None:
        rng = random.Random(42)  # fixed seed so it's reproducible
        notes = {
            "food": ["supermarket", "lunch", "coffee", "pizza"],
            "transport": ["bus ticket", "fuel", "taxi"],
            "rent": ["monthly rent"],
            "bills": ["electricity", "internet", "water"],
            "fun": ["cinema", "concert", "game"],
            "health": ["pharmacy", "dentist"],
            "other": ["gift", "misc"],
        }
        today = date.today()
        for _ in range(count):
            category = rng.choice(CATEGORIES)
            day = (today - timedelta(days=rng.randint(0, 75))).isoformat()
            amount = rng.uniform(3, 60) if category != "rent" else 350.0
            self.add(day, category, amount, rng.choice(notes[category]))


# ---------------------------------------------------------------
# User Input
# ---------------------------------------------------------------
def ask(prompt: str) -> str:
    return input(prompt).strip()


def ask_float(prompt: str, minimum: float = 0.0) -> float:
    while True:
        text = ask(prompt).replace(",", ".")
        try:
            value = float(text)
            if value >= minimum:
                return value
        except ValueError:
            pass
        print(f"  Please enter a number >= {minimum}.")


def ask_int(prompt: str) -> int:
    while True:
        try:
            return int(ask(prompt))
        except ValueError:
            print("  Please enter a whole number.")


def ask_date(prompt: str) -> str:
    """Accepts date in YYYY-MM-DD format. Empty = today."""
    while True:
        text = ask(prompt)
        if not text:
            return date.today().isoformat()
        try:
            return datetime.strptime(text, "%Y-%m-%d").date().isoformat()
        except ValueError:
            print("  Use the format YYYY-MM-DD (e.g. 2026-10-17).")


def ask_category(prompt: str, allow_empty: bool = False) -> str:
    print("  Categories: " + ", ".join(CATEGORIES))
    while True:
        text = ask(prompt).lower()
        if text in CATEGORIES or (allow_empty and text == ""):
            return text
        print("  Unknown category.")


def ask_month(prompt: str) -> str | None:
    """Accepts YYYY-MM. Empty = all months (returns None)."""
    while True:
        text = ask(prompt)
        if not text:
            return None
        try:
            datetime.strptime(text, "%Y-%m")
            return text
        except ValueError:
            print("  Use the format YYYY-MM (e.g. 2026-10).")


# ---------------------------------------------------------------
# Display
# ---------------------------------------------------------------
def bar(value: float, maximum: float, width: int = 30) -> str:
    if maximum <= 0:
        return ""
    return "#" * max(1, round(value / maximum * width)) if value > 0 else ""


def print_expenses(expenses: list[Expense]) -> None:
    if not expenses:
        print("  (no expenses found)")
        return
    print(f"{'ID':<5}{'Date':<12}{'Category':<11}{'Amount':>10}  Note")
    print("-" * 56)
    for e in expenses:
        print(f"{e.id:<5}{e.day:<12}{e.category:<11}{e.amount:>10.2f}  {e.note}")
    print("-" * 56)
    print(f"{'Total':<28}{sum(e.amount for e in expenses):>10.2f}")


def print_category_summary(totals: dict[str, float]) -> None:
    if not totals:
        print("  (nothing to summarise)")
        return
    grand = sum(totals.values())
    top = max(totals.values())
    for cat, value in totals.items():
        share = value / grand * 100
        print(f"{cat:<11}{value:>9.2f} {share:>5.1f}%  {bar(value, top)}")
    print(f"{'TOTAL':<11}{grand:>9.2f}")


def print_monthly(totals: dict[str, float]) -> None:
    if not totals:
        print("  (no data)")
        return
    top = max(totals.values())
    previous = None
    for month, value in totals.items():
        change = ""
        if previous:
            diff = (value - previous) / previous * 100
            change = f"({diff:+.0f}% vs previous)"
        print(f"{month}  {value:>9.2f}  {bar(value, top, 25):<25} {change}")
        previous = value
    avg = sum(totals.values()) / len(totals)
    print(f"Average per month: {avg:.2f}")


def print_budget_report(rows: list[tuple[str, float, float]], month: str) -> None:
    if not rows:
        print("  No budgets set yet. Use option 7 to create one.")
        return
    print(f"Budget status for {month}")
    for cat, spent, limit in rows:
        pct = spent / limit * 100
        flag = "OVER BUDGET!" if spent > limit else ("close to limit" if pct >= 80 else "ok")
        print(f"{cat:<11}{spent:>8.2f} / {limit:<8.2f} {pct:>5.0f}%  {flag}")


MENU = """
===== EXPENSE TRACKER =====
 1. Add expense
 2. List expenses (optional filters)
 3. Delete expense
 4. Summary by category
 5. Monthly totals
 6. Budget status
 7. Set / remove a budget
 8. Export to CSV
 9. Add sample data
 0. Exit
"""


# ---------------------------------------------------------------
# Main program
# ---------------------------------------------------------------
def main() -> None:
    tracker = ExpenseTracker()
    print(f"Loaded {len(tracker.expenses)} expense(s) from {tracker.path}")

    while True:
        print(MENU)
        choice = ask("Choice: ")

        if choice == "1":
            day = ask_date("Date (YYYY-MM-DD, empty = today): ")
            category = ask_category("Category: ")
            amount = ask_float("Amount: ", minimum=0.01)
            note = ask("Note (optional): ")
            e = tracker.add(day, category, amount, note)
            print(f"Saved expense #{e.id}.")

        elif choice == "2":
            category = ask_category("Filter by category (empty = all): ", allow_empty=True)
            month = ask_month("Filter by month YYYY-MM (empty = all): ")
            print_expenses(tracker.filter(category or None, month))

        elif choice == "3":
            expense_id = ask_int("ID to delete: ")
            print("Deleted." if tracker.delete(expense_id) else "No expense with that ID.")

        elif choice == "4":
            month = ask_month("Month YYYY-MM (empty = all time): ")
            print_category_summary(tracker.totals_by_category(month))

        elif choice == "5":
            print_monthly(tracker.totals_by_month())

        elif choice == "6":
            month = ask_month("Month YYYY-MM (empty = current): ") or date.today().strftime("%Y-%m")
            print_budget_report(tracker.budget_report(month), month)

        elif choice == "7":
            category = ask_category("Category: ")
            limit = ask_float("Monthly limit (0 = remove): ")
            tracker.set_budget(category, limit)
            print("Budget updated.")

        elif choice == "8":
            name = ask("File name [expenses.csv]: ") or "expenses.csv"
            count = tracker.export_csv(Path(name))
            print(f"Exported {count} expense(s) to {name}.")

        elif choice == "9":
            tracker.add_sample_data()
            print("Sample data added.")

        elif choice == "0":
            print("Goodbye!")
            break

        else:
            print("Invalid choice.")


if __name__ == "__main__":
    try:
        main()
    except (KeyboardInterrupt, EOFError):
        print("\nBye!")
