// The Ada runtime that every translated program uses.
// Copyright (C) 2026 Ivan Janevski
// SPDX-License-Identifier: GPL-3.0-or-later

#include "runtime.h"

// Return the spec of Mpd_Runtime.
std::string_view runtime_spec()
{
    return R"ADA(--  Ada spec for the runtime of programs mpdcc translates.
--
--  Copyright (C) 2026 Ivan Janevski
--
--  mpdcc is free software; you can redistribute it and/or modify it
--  under the terms of the GNU General Public License as published by
--  the Free Software Foundation; either version 3 of the License, or
--  (at your option) any later version.
--
--  mpdcc is distributed in the hope that it will be useful, but WITHOUT
--  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
--  or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
--  License for more details.
--
--  You should have received a copy of the GNU General Public License
--  along with mpdcc; if not, see <https://www.gnu.org/licenses/>.

package Mpd_Runtime is

   --  A process's number, from 1.
   subtype Pid is Positive;

   --  An argument or a result: an int, a bool or a char in I, a real in R.
   type Value is record
      I : Integer := 0;
      R : Long_Float := 0.0;
   end record;

   --  The most formals an op may have.
   Max_Args : constant := 16;

   --  The arguments of one invocation.
   type Value_Array is array (1 .. Max_Args) of Value;

   function Val (X : Integer) return Value;
   function Val (X : Boolean) return Value;
   function Val (X : Character) return Value;
   function Val (X : Long_Float) return Value;
   function To_Int (X : Value) return Integer;
   function To_Bool (X : Value) return Boolean;
   function To_Char (X : Value) return Character;
   function To_Real (X : Value) return Long_Float;

   --  Lets the process that called an op wait until the op is served.
   protected type Completion is
      entry Wait;
      procedure Finish;
   private
      Finished : Boolean := False;
   end Completion;

   type Invocation;
   type Invocation_Access is access Invocation;

   --  One invocation of an op, waiting in the op's queue until an in serves it.
   type Invocation is limited record
      Op      : Natural := 0;
      Seq     : Natural := 0;
      Caller  : Pid := 1;
      Is_Call : Boolean := False;
      Args    : Value_Array;
      Result  : Value;
      Prev    : Invocation_Access;
      Next    : Invocation_Access;
      Signal  : Completion;
   end record;

   --  A semaphore.
   protected type Semaphore (Initial : Natural) is
      entry Seize;
      procedure Release;
      procedure Try (Taken : out Boolean);
   private
      Count : Natural := Initial;
   end Semaphore;

   --  Read the options, such as --trace, and make queues for Ops ops.
   procedure Init (Ops : Natural);

   --  Name op Op, such as BoundedBuffer.deposit, for traces.
   procedure Set_Op_Name (Op : Positive; Name : String);

   --  The name of op Op.
   function Op_Name (Op : Positive) return String;

   --  A name for the process a send to op Op's proc creates, such as deposit#3.
   function Spawn_Name (Op : Positive) return String;

   --  Add a process named Name and return its number.
   function Register (Name : String) return Pid;

   --  The name of process Id.
   function Name_Of (Id : Pid) return String;

   --  Let the processes run, once every one of them exists.
   procedure Start;

   --  Wait, as a process's first step, until Start.
   procedure Wait_Start;

   --  Whether --trace was given.
   function Tracing return Boolean;

   --  Print, under --trace, a line about process Id.
   procedure Trace (Id : Pid; Text : String);

   --  Mark process Id as running again.
   procedure Running (Id : Pid);

   --  Mark process Id as terminated.
   procedure Done (Id : Pid);

   --  Report that process Id failed and stop the program with status 1.
   procedure Fail (Id : Pid; Message : String) with No_Return;

   --  Run a hook, a command nothing defines: print it, then pause a moment.
   procedure Hook (Id : Pid; Call : String);

   --  Print a line, as write does.
   procedure Write_Line (Text : String);

   --  A random number from 0 to N - 1.
   function Random (N : Integer) return Integer;

   --  P and V on a semaphore; P marks process Id as waiting while it blocks.
   procedure P (S : in out Semaphore; Id : Pid; What : String);
   procedure V (S : in out Semaphore);

   --  A new invocation of op Op by process Caller, which waits for it if Is_Call.
   function New_Invocation (Op : Positive; Caller : Pid; Is_Call : Boolean)
     return Invocation_Access;

   --  Add an invocation to its op's queue and wake the processes in in statements.
   procedure Enqueue (Inv : Invocation_Access);

   --  Wait, as process Id, until an invocation is served.
   procedure Await (Id : Pid; Inv : Invocation_Access; What : String);

   --  Free an invocation.
   procedure Free (Inv : in out Invocation_Access);

   --  End serving an invocation: wake its caller, or free it after a send.
   procedure Finish (Inv : in out Invocation_Access);

   --  The number of invocations of op Op waiting in its queue, ?op.
   function Pending (Op : Positive) return Natural;

   --  Lock and unlock the queues, around an in statement's scan.
   procedure Seize;
   procedure Release;

   --  The oldest invocation of op Op, under the lock.
   function First (Op : Positive) return Invocation_Access;

   --  Take an invocation out of its queue, under the lock.
   procedure Remove (Inv : Invocation_Access);

   --  A number that changes with every new invocation, read under the lock.
   function Version return Natural;

   --  Wait, as process Id, until a new invocation arrives after Version was Seen.
   procedure Sleep (Id : Pid; Seen : Natural; What : String);

   --  Values as traces, hooks and write print them.
   function Img (X : Integer) return String;
   function Img (X : Long_Float) return String;
   function Img (X : Character) return String;
   function Img (X : Boolean) return String;

end Mpd_Runtime;
)ADA";
}

// Return the body of Mpd_Runtime.
std::string_view runtime_body()
{
    return R"ADA(--  Ada body for the runtime of programs mpdcc translates.
--
--  Copyright (C) 2026 Ivan Janevski
--
--  mpdcc is free software; you can redistribute it and/or modify it
--  under the terms of the GNU General Public License as published by
--  the Free Software Foundation; either version 3 of the License, or
--  (at your option) any later version.
--
--  mpdcc is distributed in the hope that it will be useful, but WITHOUT
--  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
--  or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
--  License for more details.
--
--  You should have received a copy of the GNU General Public License
--  along with mpdcc; if not, see <https://www.gnu.org/licenses/>.

with Ada.Calendar;
with Ada.Command_Line;
with Ada.Containers.Vectors;
with Ada.Long_Float_Text_IO;
with Ada.Numerics.Float_Random;
with Ada.Strings.Fixed;
with Ada.Strings.Unbounded;
with Ada.Text_IO;
with Ada.Unchecked_Deallocation;
with GNAT.OS_Lib;

package body Mpd_Runtime is

   use Ada.Strings.Unbounded;

   --  What a process is doing: running, waiting in call or P, waiting in in or
   --  receive, or done.
   type State_Kind is (Running_State, Blocked_State, Serving_State, Done_State);

   package Kind_Vectors is new Ada.Containers.Vectors (Positive, State_Kind);
   package Text_Vectors is new Ada.Containers.Vectors (Positive, Unbounded_String);

   type Access_Array is array (Positive range <>) of Invocation_Access;
   type Access_Table is access Access_Array;
   type Count_Array is array (Positive range <>) of Natural with Atomic_Components;
   type Count_Table is access Count_Array;
   type Name_Array is array (Positive range <>) of Unbounded_String;
   type Name_Table is access Name_Array;

   --  Options.
   Trace_On : Boolean  := False;
   Quiet    : Boolean  := False;
   Delay_Ms : Natural  := 10;
   Time_Out : Duration := 0.0;
   Seed     : Integer  := 0;
   Seeded   : Boolean  := False;

   --  The ops' queues and names, made by Init; the queues change only under Queues_Lock.
   Heads    : Access_Table;
   Tails    : Access_Table;
   Counts   : Count_Table;
   Op_Names : Name_Table;
   Spawns   : Count_Table;
   Next_Seq : Natural := 0;

   procedure Free_Invocation is new Ada.Unchecked_Deallocation (Invocation, Invocation_Access);

   --  A lock like a binary semaphore.
   protected type Lock_Type is
      entry Seize;
      procedure Release;
   private
      Busy : Boolean := False;
   end Lock_Type;

   protected body Lock_Type is
      entry Seize when not Busy is
      begin
         Busy := True;
      end Seize;

      procedure Release is
      begin
         Busy := False;
      end Release;
   end Lock_Type;

   --  Serializes output lines.
   Output_Lock : Lock_Type;

   --  Guards the ops' queues.
   Queues_Lock : Lock_Type;

   --  Holds the processes back until Start.
   protected Gate is
      entry Wait;
      procedure Open;
   private
      Is_Open : Boolean := False;
   end Gate;

   protected body Gate is
      entry Wait when Is_Open is
      begin
         null;
      end Wait;

      procedure Open is
      begin
         Is_Open := True;
      end Open;
   end Gate;

   --  Wakes every sleeping in statement when an invocation arrives.
   protected Bell is
      function Current return Natural;
      procedure Ring;
      entry Wait (Seen : Natural);
   private
      entry Sleeping (Seen : Natural);
      Rings : Natural := 0;
      Open : Boolean := False;
   end Bell;

   protected body Bell is
      function Current return Natural is (Rings);

      procedure Ring is
      begin
         Rings := (if Rings = Natural'Last then 0 else Rings + 1);
         Open := Sleeping'Count > 0;
      end Ring;

      entry Wait (Seen : Natural) when True is
      begin
         if Seen = Rings then
            requeue Sleeping;
         end if;
      end Wait;

      entry Sleeping (Seen : Natural) when Open is
      begin
         if Sleeping'Count = 0 then
            Open := False;
         end if;
      end Sleeping;
   end Bell;

   --  Each process's name and state, and a counter of changes for the watchdog.
   protected State is
      procedure Add (Name : String; Id : out Pid);
      procedure Set (Id : Pid; Kind : State_Kind; What : String);
      function Get (Id : Pid) return State_Kind;
      function Name_Of (Id : Pid) return String;
      function What_Of (Id : Pid) return String;
      function Count return Natural;
      function Steps return Natural;
      function Done_Count return Natural;
      function Blocked_Count return Natural;
      function Serving_Count return Natural;
   private
      Kinds : Kind_Vectors.Vector;
      Names : Text_Vectors.Vector;
      Whats : Text_Vectors.Vector;
      Changes : Natural := 0;
      Dones : Natural := 0;
      Blocks : Natural := 0;
      Serves : Natural := 0;
   end State;

   protected body State is
      procedure Add (Name : String; Id : out Pid) is
      begin
         Kinds.Append (Running_State);
         Names.Append (To_Unbounded_String (Name));
         Whats.Append (Null_Unbounded_String);
         Id := Kinds.Last_Index;
         Changes := (if Changes = Natural'Last then 0 else Changes + 1);
      end Add;

      procedure Set (Id : Pid; Kind : State_Kind; What : String) is
         Old : constant State_Kind := Kinds (Id);
      begin
         if Old = Done_State then
            return;
         end if;
         case Old is
            when Blocked_State => Blocks := Blocks - 1;
            when Serving_State => Serves := Serves - 1;
            when others => null;
         end case;
         case Kind is
            when Blocked_State => Blocks := Blocks + 1;
            when Serving_State => Serves := Serves + 1;
            when Done_State => Dones := Dones + 1;
            when Running_State => null;
         end case;
         Kinds (Id) := Kind;
         Whats (Id) := To_Unbounded_String (What);
         if Kind = Running_State or else Kind = Done_State then
            Changes := (if Changes = Natural'Last then 0 else Changes + 1);
         end if;
      end Set;

      function Get (Id : Pid) return State_Kind is (Kinds (Id));
      function Name_Of (Id : Pid) return String is (To_String (Names (Id)));
      function What_Of (Id : Pid) return String is (To_String (Whats (Id)));
      function Count return Natural is (Natural (Kinds.Length));
      function Steps return Natural is (Changes);
      function Done_Count return Natural is (Dones);
      function Blocked_Count return Natural is (Blocks);
      function Serving_Count return Natural is (Serves);
   end State;

   --  Random numbers for random and for the hooks' pauses.
   protected Dice is
      procedure Reset;
      procedure Roll (N : Positive; Result : out Positive);
   private
      G : Ada.Numerics.Float_Random.Generator;
   end Dice;

   protected body Dice is
      procedure Reset is
      begin
         if Seeded then
            Ada.Numerics.Float_Random.Reset (G, Seed);
         else
            Ada.Numerics.Float_Random.Reset (G);
         end if;
      end Reset;

      procedure Roll (N : Positive; Result : out Positive) is
         R : constant Float := Ada.Numerics.Float_Random.Random (G);
      begin
         Result := Positive'Min (N, 1 + Integer (Float'Floor (R * Float (N))));
      end Roll;
   end Dice;

   --  Print a line to standard output, whole.
   procedure Put (Line : String) is
   begin
      Output_Lock.Seize;
      Ada.Text_IO.Put_Line (Line);
      Ada.Text_IO.Flush;
      Output_Lock.Release;
   end Put;

   --  Print a line to standard error and stop the program.
   procedure Stop (Line : String; Status : Integer) with No_Return is
   begin
      Output_Lock.Seize;
      Ada.Text_IO.Flush;
      if Line'Length > 0 then
         Ada.Text_IO.Put_Line (Ada.Text_IO.Standard_Error, Line);
      end if;
      Ada.Text_IO.Flush (Ada.Text_IO.Standard_Error);
      GNAT.OS_Lib.OS_Exit (Status);
   end Stop;

   --  Watches for the end of the program, a deadlock and the end of --timeout.
   task Watchdog is
      entry Go;
   end Watchdog;

   task body Watchdog is
      use type Ada.Calendar.Time;
      Begun : Ada.Calendar.Time;
      Last : Natural := 0;
      Still : Natural := 0;
   begin
      select
         accept Go;
      or
         terminate;
      end select;
      Begun := Ada.Calendar.Clock;
      loop
         delay 0.1;
         exit when State.Done_Count = State.Count;
         if Time_Out > 0.0 and then Ada.Calendar.Clock - Begun >= Time_Out then
            Stop ("mpd: stopped after --timeout", 0);
         end if;
         if State.Blocked_Count + State.Serving_Count > 0
           and then State.Blocked_Count + State.Serving_Count + State.Done_Count = State.Count
           and then State.Steps = Last
         then
            Still := Still + 1;
         else
            Still := 0;
         end if;
         Last := State.Steps;
         if Still >= 3 then
            if State.Blocked_Count = 0 then
               Stop ((if Trace_On then "mpd: every process waits in in or receive" else ""), 0);
            end if;
            Output_Lock.Seize;
            Ada.Text_IO.Flush;
            Ada.Text_IO.Put_Line (Ada.Text_IO.Standard_Error,
                                  "mpd: deadlock, every process is waiting:");
            for Id in 1 .. State.Count loop
               if State.Get (Id) = Blocked_State or else State.Get (Id) = Serving_State then
                  Ada.Text_IO.Put_Line (Ada.Text_IO.Standard_Error,
                                        "  " & State.Name_Of (Id) & ": " & State.What_Of (Id));
               end if;
            end loop;
            Ada.Text_IO.Flush (Ada.Text_IO.Standard_Error);
            GNAT.OS_Lib.OS_Exit (3);
         end if;
      end loop;
   end Watchdog;

   --  Print the runtime's options and exit.
   procedure Usage is
   begin
      Ada.Text_IO.Put_Line ("Usage: " & Ada.Command_Line.Command_Name & " [options]");
      Ada.Text_IO.Put_Line ("  --trace          print every invocation and when it is served");
      Ada.Text_IO.Put_Line ("  --quiet          don't print hooks");
      Ada.Text_IO.Put_Line ("  --delay=MS       pause each hook up to MS milliseconds (default 10)");
      Ada.Text_IO.Put_Line ("  --seed=N         seed the random choices with N");
      Ada.Text_IO.Put_Line ("  --timeout=S      stop after S seconds, with status 0");
      GNAT.OS_Lib.OS_Exit (0);
   end Usage;

   --  Read the program's options.
   procedure Parse_Options is
      function Value (Arg, Prefix : String) return String is
        (Arg (Arg'First + Prefix'Length .. Arg'Last));
   begin
      for I in 1 .. Ada.Command_Line.Argument_Count loop
         declare
            Arg : constant String := Ada.Command_Line.Argument (I);
            function Starts (Prefix : String) return Boolean is
              (Arg'Length > Prefix'Length
               and then Arg (Arg'First .. Arg'First + Prefix'Length - 1) = Prefix);
         begin
            if Arg = "--trace" then
               Trace_On := True;
            elsif Arg = "--quiet" then
               Quiet := True;
            elsif Arg = "--help" or else Arg = "-h" then
               Usage;
            elsif Starts ("--delay=") then
               Delay_Ms := Natural'Value (Value (Arg, "--delay="));
            elsif Starts ("--seed=") then
               Seed := Integer'Value (Value (Arg, "--seed="));
               Seeded := True;
            elsif Starts ("--timeout=") then
               Time_Out := Duration'Value (Value (Arg, "--timeout="));
            else
               Stop ("mpd: unknown option '" & Arg & "'", 2);
            end if;
         exception
            when Constraint_Error =>
               Stop ("mpd: bad value in '" & Arg & "'", 2);
         end;
      end loop;
   end Parse_Options;

   function Val (X : Integer) return Value is ((I => X, R => 0.0));
   function Val (X : Boolean) return Value is ((I => Boolean'Pos (X), R => 0.0));
   function Val (X : Character) return Value is ((I => Character'Pos (X), R => 0.0));
   function Val (X : Long_Float) return Value is ((I => 0, R => X));
   function To_Int (X : Value) return Integer is (X.I);
   function To_Bool (X : Value) return Boolean is (X.I /= 0);
   function To_Char (X : Value) return Character is (Character'Val (X.I));
   function To_Real (X : Value) return Long_Float is (X.R);

   protected body Completion is
      entry Wait when Finished is
      begin
         null;
      end Wait;

      procedure Finish is
      begin
         Finished := True;
      end Finish;
   end Completion;

   protected body Semaphore is
      entry Seize when Count > 0 is
      begin
         Count := Count - 1;
      end Seize;

      procedure Release is
      begin
         Count := Count + 1;
      end Release;

      procedure Try (Taken : out Boolean) is
      begin
         Taken := Count > 0;
         if Taken then
            Count := Count - 1;
         end if;
      end Try;
   end Semaphore;

   procedure Init (Ops : Natural) is
   begin
      Parse_Options;
      Heads := new Access_Array'(1 .. Ops => null);
      Tails := new Access_Array'(1 .. Ops => null);
      Counts := new Count_Array'(1 .. Ops => 0);
      Spawns := new Count_Array'(1 .. Ops => 0);
      Op_Names := new Name_Array (1 .. Ops);
      Dice.Reset;
   end Init;

   procedure Set_Op_Name (Op : Positive; Name : String) is
   begin
      Op_Names (Op) := To_Unbounded_String (Name);
   end Set_Op_Name;

   function Op_Name (Op : Positive) return String is (To_String (Op_Names (Op)));

   function Spawn_Name (Op : Positive) return String is
      Number : Natural;
   begin
      Queues_Lock.Seize;
      Spawns (Op) := Spawns (Op) + 1;
      Number := Spawns (Op);
      Queues_Lock.Release;
      return Op_Name (Op) & "#" & Img (Number);
   end Spawn_Name;

   function Register (Name : String) return Pid is
      Id : Pid;
   begin
      State.Add (Name, Id);
      return Id;
   end Register;

   function Name_Of (Id : Pid) return String is (State.Name_Of (Id));

   procedure Start is
   begin
      Gate.Open;
      Watchdog.Go;
   end Start;

   procedure Wait_Start is
   begin
      Gate.Wait;
   end Wait_Start;

   function Tracing return Boolean is (Trace_On);

   procedure Trace (Id : Pid; Text : String) is
   begin
      if Trace_On then
         Put (Name_Of (Id) & " " & Text);
      end if;
   end Trace;

   procedure Running (Id : Pid) is
   begin
      State.Set (Id, Running_State, "");
   end Running;

   procedure Done (Id : Pid) is
   begin
      State.Set (Id, Done_State, "");
   end Done;

   procedure Fail (Id : Pid; Message : String) is
   begin
      Stop ("mpd: " & Name_Of (Id) & ": " & Message, 1);
   end Fail;

   procedure Hook (Id : Pid; Call : String) is
      Ms : Positive := 1;
   begin
      if not Quiet then
         Put (Name_Of (Id) & ": " & Call);
      end if;
      if Delay_Ms > 0 then
         Dice.Roll (Delay_Ms + 1, Ms);
         delay Duration (Ms - 1) / 1000.0;
      end if;
      if Trace_On then
         Put (Name_Of (Id) & ": " & Call & " end");
      end if;
   end Hook;

   procedure Write_Line (Text : String) is
   begin
      Put (Text);
   end Write_Line;

   function Random (N : Integer) return Integer is
      R : Positive := 1;
   begin
      if N <= 1 then
         return 0;
      end if;
      Dice.Roll (N, R);
      return R - 1;
   end Random;

   procedure P (S : in out Semaphore; Id : Pid; What : String) is
      Taken : Boolean;
   begin
      S.Try (Taken);
      if not Taken then
         State.Set (Id, Blocked_State, What);
         S.Seize;
         Running (Id);
      end if;
   end P;

   procedure V (S : in out Semaphore) is
   begin
      S.Release;
   end V;

   function New_Invocation (Op : Positive; Caller : Pid; Is_Call : Boolean)
     return Invocation_Access
   is
      Inv : constant Invocation_Access := new Invocation;
   begin
      Inv.Op := Op;
      Inv.Caller := Caller;
      Inv.Is_Call := Is_Call;
      return Inv;
   end New_Invocation;

   procedure Enqueue (Inv : Invocation_Access) is
      Op : constant Positive := Inv.Op;
   begin
      Queues_Lock.Seize;
      Next_Seq := (if Next_Seq = Natural'Last then 0 else Next_Seq + 1);
      Inv.Seq := Next_Seq;
      Inv.Prev := Tails (Op);
      Inv.Next := null;
      if Tails (Op) = null then
         Heads (Op) := Inv;
      else
         Tails (Op).Next := Inv;
      end if;
      Tails (Op) := Inv;
      Counts (Op) := Counts (Op) + 1;
      Bell.Ring;
      Queues_Lock.Release;
   end Enqueue;

   procedure Await (Id : Pid; Inv : Invocation_Access; What : String) is
   begin
      State.Set (Id, Blocked_State, What);
      Inv.Signal.Wait;
      Running (Id);
   end Await;

   procedure Free (Inv : in out Invocation_Access) is
   begin
      Free_Invocation (Inv);
   end Free;

   procedure Finish (Inv : in out Invocation_Access) is
   begin
      if Inv.Is_Call then
         Inv.Signal.Finish;
         Inv := null;
      else
         Free_Invocation (Inv);
      end if;
   end Finish;

   function Pending (Op : Positive) return Natural is (Counts (Op));

   procedure Seize is
   begin
      Queues_Lock.Seize;
   end Seize;

   procedure Release is
   begin
      Queues_Lock.Release;
   end Release;

   function First (Op : Positive) return Invocation_Access is (Heads (Op));

   procedure Remove (Inv : Invocation_Access) is
      Op : constant Positive := Inv.Op;
   begin
      if Inv.Prev = null then
         Heads (Op) := Inv.Next;
      else
         Inv.Prev.Next := Inv.Next;
      end if;
      if Inv.Next = null then
         Tails (Op) := Inv.Prev;
      else
         Inv.Next.Prev := Inv.Prev;
      end if;
      Inv.Prev := null;
      Inv.Next := null;
      Counts (Op) := Counts (Op) - 1;
   end Remove;

   function Version return Natural is (Bell.Current);

   procedure Sleep (Id : Pid; Seen : Natural; What : String) is
   begin
      State.Set (Id, Serving_State, What);
      Bell.Wait (Seen);
      Running (Id);
   end Sleep;

   function Img (X : Integer) return String is
     (Ada.Strings.Fixed.Trim (Integer'Image (X), Ada.Strings.Both));

   function Img (X : Long_Float) return String is
      Buffer : String (1 .. 64);
   begin
      if abs X >= 1.0E15 then
         Ada.Long_Float_Text_IO.Put (Buffer, X, Aft => 9, Exp => 3);
         return Ada.Strings.Fixed.Trim (Buffer, Ada.Strings.Both);
      end if;
      Ada.Long_Float_Text_IO.Put (Buffer, X, Aft => 9, Exp => 0);
      declare
         S : constant String := Ada.Strings.Fixed.Trim (Buffer, Ada.Strings.Both);
         Last : Natural := S'Last;
      begin
         while Last > S'First and then S (Last) = '0' and then S (Last - 1) /= '.' loop
            Last := Last - 1;
         end loop;
         return S (S'First .. Last);
      end;
   end Img;

   function Img (X : Character) return String is ("'" & X & "'");

   function Img (X : Boolean) return String is (if X then "true" else "false");

end Mpd_Runtime;
)ADA";
}
