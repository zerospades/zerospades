/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <cstddef>
#include <deque>
#include <exception>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include <Core/Debug.h>

namespace spades {
	namespace gui {
		/**
		 * Bounded undo/redo history for an editor's document.
		 *
		 * The history stores reversible changes only and knows nothing about the
		 * document, the renderer or the camera: each change to the document is a
		 * `Record` the editor defines, and everything else an undo step restores
		 * (the selection, and the like) is a `State` it snapshots. The host
		 * implements `Sink` to apply a record to the live document either way,
		 * and the history drives it in the correct order on Undo()/Redo(),
		 * keeping the tricky bits (reverse replay, state restoration, merging)
		 * in one place and the host a thin adapter.
		 *
		 * Each edit records into a `Step`, a scope that lives no longer than the
		 * call making the edit; nested steps coalesce, and a step is committed
		 * only if it recorded a change or changed the state. Steps committed
		 * during one user action (a press-drag-release, a key press) then merge
		 * into a single undo step, so a paint stroke or a multi-step edit
		 * undoes at once. Nothing stays open between events, so undo and
		 * redo work at any moment. The history is capped at a number of steps
		 * and an amount of memory (see Limits), evicting the oldest.
		 *
		 * Recording never fails a command half way: a step that cannot be kept
		 * (out of memory) would leave the history unable to replay back to the
		 * document, so the history is cleared instead of left inconsistent.
		 *
		 * `State` is a value type with `operator!=` and
		 * `std::size_t UnsharedBytes(const State& other) const`, the heap bytes
		 * it holds that `other` does not share with it.
		 */
		template <class Record, class State> class UndoHistory {
		public:
			/** Applies records to the live document and snapshots the rest. */
			class Sink {
			public:
				virtual ~Sink() {}
				// Apply `record` to the document: its change when `forward`, else
				// its inverse. Records are replayed in recorded order forward and
				// in reverse order backward, so each finds the document as it was
				// when it was recorded.
				virtual void UndoApply(const Record& record, bool forward) = 0;
				// Read / replace everything besides the document that a step restores.
				virtual State UndoSnapshotState() const = 0;
				virtual void UndoRestoreState(const State& state) = 0;
				// Called once after a step has been applied, to refresh derived state
				// (the render model, etc.).
				virtual void UndoReplayed() = 0;
			};

			/**
			 * How much history is kept. Past either cap the oldest steps go; the
			 * most recent step is always kept, even if it alone exceeds `bytes`.
			 * The memory cap keeps neither big edits nor the state kept per step
			 * from growing the history without bound.
			 */
			struct Limits {
				std::size_t steps = 256;
				std::size_t bytes = std::size_t(256) << 20;
			};

			explicit UndoHistory(Sink& sink, Limits limits = Limits())
			    : sink(sink), limits(limits) {}

			// --- recording ----------------------------------------------------

			/**
			 * One journaled edit, recorded for the lifetime of the scope. Nested
			 * steps coalesce into the outermost, which commits on leaving its scope
			 * (also when an exception unwinds it) if anything changed.
			 */
			class Step {
			public:
				Step(UndoHistory& history, const std::string& label) : history(history) {
					history.Begin(label);
				}
				~Step() { history.End(); } // never throws, so it is safe while unwinding
				Step(const Step&) = delete;
				Step& operator=(const Step&) = delete;

			private:
				UndoHistory& history;
			};

			/**
			 * Starts a user action: every step committed until EndAction (or the
			 * next BeginAction, Undo, Redo or Clear) merges into one undo step.
			 * Actions hold nothing open, so ending one is never required for
			 * the history to work; it only stops the merging.
			 */
			void BeginAction() {
				action = ++nextAction;
				if (action == 0)
					action = ++nextAction; // 0 means "no action"; skip it on wrap-around
			}
			void EndAction() { action = 0; }

			/** Appends a change of the document to the step open; ignored outside one. */
			void Append(Record record) {
				if (depth == 0)
					return; // only recorded inside a step
				pending.records.push_back(std::move(record));
			}

			// --- history ------------------------------------------------------
			bool CanUndo() const { return !undoGroups.empty(); }
			bool CanRedo() const { return !redoGroups.empty(); }
			std::string UndoLabel() const { return undoGroups.empty() ? "" : undoGroups.back().label; }
			std::string RedoLabel() const { return redoGroups.empty() ? "" : redoGroups.back().label; }

			bool Undo() { // false if there was nothing to undo
				if (depth != 0 || undoGroups.empty())
					return false;
				action = 0; // nothing done after this merges into what came before
				Group g = std::move(undoGroups.back());
				undoGroups.pop_back();
				for (auto it = g.records.rbegin(); it != g.records.rend(); ++it)
					sink.UndoApply(*it, false);
				sink.UndoRestoreState(g.before);
				sink.UndoReplayed();
				documentId = g.documentBefore;
				redoGroups.push_back(std::move(g));
				return true;
			}

			bool Redo() {
				if (depth != 0 || redoGroups.empty())
					return false;
				action = 0;
				Group g = std::move(redoGroups.back());
				redoGroups.pop_back();
				for (const Record& r : g.records)
					sink.UndoApply(r, true);
				sink.UndoRestoreState(g.after);
				sink.UndoReplayed();
				documentId = g.documentAfter;
				undoGroups.push_back(std::move(g));
				return true;
			}

			void Clear() noexcept {
				undoGroups.clear();
				redoGroups.clear();
				pending = Group();
				depth = 0;
				action = 0;
				// Ids are never reused: the document kept its unrecorded changes (when
				// a step could not be recorded), so it must not match an id it was
				// saved at.
				documentId = ++nextDocumentId;
				totalBytes = 0;
			}

			// Id of the current state of the *document*, for its dirty/clean flag.
			// Steps that record no change of it (a selection, say) leave it
			// unchanged, so merely selecting never marks the document modified. A
			// new state never takes an id used before, Clear included, so a stale
			// saved id never matches it.
			long DocumentStateId() const { return documentId; }

		private:
			struct Group {
				std::string label;
				std::vector<Record> records;
				State before, after;
				std::size_t bytes = 0; // what keeping it costs; see BytesOf
				long documentBefore = 0, documentAfter = 0;
				unsigned action = 0; // the user action it was recorded in, 0 for none
			};

			// Open / close the pending group; only through Step, so they pair up.
			void Begin(const std::string& label) {
				if (depth == 0) {
					pending = Group();
					pending.label = label;
					pending.before = sink.UndoSnapshotState();
				}
				depth++;
			}

			void End() noexcept {
				if (depth == 0)
					return; // unbalanced End; ignore defensively
				if (--depth > 0)
					return; // still inside an outer step
				// Runs from a destructor, possibly while an exception unwinds, so
				// nothing may escape. The edit itself has happened either way: a
				// step that cannot be kept would leave undo replaying into the
				// wrong document, so the history starts afresh instead.
				try {
					pending.after = sink.UndoSnapshotState();
					if (!pending.records.empty() || pending.before != pending.after)
						Commit();
					else
						pending = Group();
				} catch (const std::exception& ex) {
					SPLog("Undo history cleared: could not record '%s': %s",
					      pending.label.c_str(), ex.what());
					Clear();
				} catch (...) {
					SPLog("Undo history cleared: could not record '%s'", pending.label.c_str());
					Clear();
				}
			}

			void Commit() {
				pending.documentBefore = documentId;
				// Only steps that change the document advance its id (which drives
				// the dirty flag); steps that change only the rest of the state
				// share the surrounding document state.
				pending.documentAfter = pending.records.empty() ? documentId : ++nextDocumentId;
				documentId = pending.documentAfter;
				pending.action = action;

				for (const Group& g : redoGroups)
					totalBytes -= g.bytes;
				redoGroups.clear(); // a fresh edit invalidates the redo branch

				if (action != 0 && !undoGroups.empty() && undoGroups.back().action == action) {
					// Later in the same user action: extend its step, which keeps the
					// label and the "before" state of the action's first edit.
					Group& step = undoGroups.back();
					totalBytes -= step.bytes;
					step.records.insert(step.records.end(),
					                    std::make_move_iterator(pending.records.begin()),
					                    std::make_move_iterator(pending.records.end()));
					step.after = std::move(pending.after);
					step.documentAfter = pending.documentAfter;
					// An action that put everything back (select, then deselect)
					// leaves nothing to undo, so it leaves no step either.
					if (step.records.empty() && !(step.before != step.after)) {
						undoGroups.pop_back();
					} else {
						step.bytes = BytesOf(step);
						totalBytes += step.bytes;
					}
				} else {
					pending.bytes = BytesOf(pending);
					totalBytes += pending.bytes;
					undoGroups.push_back(std::move(pending));
				}
				pending = Group();

				// Evict the oldest history past either cap, but never the last step.
				while ((undoGroups.size() > limits.steps || totalBytes > limits.bytes) &&
				       undoGroups.size() > 1) {
					totalBytes -= undoGroups.front().bytes;
					undoGroups.pop_front();
				}
			}

			// Approximate memory a group adds: its records, plus whatever of its
			// after state it does not share with its before state. The before state
			// is the after state of the step below it, which counts it already;
			// only the oldest step's before state goes uncounted.
			static std::size_t BytesOf(const Group& g) {
				return g.records.size() * sizeof(Record) + g.after.UnsharedBytes(g.before);
			}

			Sink& sink;
			const Limits limits;
			std::deque<Group> undoGroups;
			std::deque<Group> redoGroups;
			Group pending;
			int depth = 0;
			unsigned action = 0, nextAction = 0;
			long documentId = 0, nextDocumentId = 0;
			std::size_t totalBytes = 0; // BytesOf every group held, undo + redo
		};
	} // namespace gui
} // namespace spades
