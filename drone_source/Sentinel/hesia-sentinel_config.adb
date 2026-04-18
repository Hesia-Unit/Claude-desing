with Ada.Characters.Handling;
with Ada.Strings.Fixed;
with Ada.Strings.Unbounded;
with Ada.Text_IO;
with Hesia_Sentinel_Util;

package body Hesia_Sentinel_Config is
   function Get_Value (Path, Key : String) return String is
      use Ada.Text_IO;
      F    : File_Type;
      K    : constant String := Hesia_Sentinel_Util.To_Lower (Key);
      Pos  : Natural := 0;
   begin
      if not Hesia_Sentinel_Util.File_Exists (Path) then
         return "";
      end if;
      Open (F, In_File, Path);
      while not End_Of_File (F) loop
         declare
            Line : constant String := Hesia_Sentinel_Util.Trim (Get_Line (F));
         begin
            if Line'Length = 0 then
               goto Continue;
            end if;
            if Line (Line'First) = '#' or else Line (Line'First) = ';' then
               goto Continue;
            end if;
            Pos := Ada.Strings.Fixed.Index (Line, "=");
            if Pos = 0 then
               goto Continue;
            end if;
            declare
               RawK : constant String := Hesia_Sentinel_Util.Trim (Line (Line'First .. Pos - 1));
               RawV : constant String := Hesia_Sentinel_Util.Trim (Line (Pos + 1 .. Line'Last));
            begin
               if Hesia_Sentinel_Util.To_Lower (RawK) = K then
                  Close (F);
                  return RawV;
               end if;
            end;
            <<Continue>>
            null;
         end;
      end loop;
      Close (F);
      return "";
   exception
      when others =>
         return "";
   end Get_Value;

   function Missing_Modules (Modules_Path : String) return String is
      use Ada.Strings.Unbounded;
      use Ada.Text_IO;
      F       : File_Type;
      Missing : Unbounded_String := To_Unbounded_String ("");
      First   : Boolean := True;
   begin
      if Modules_Path'Length = 0 then
         return "";
      end if;
      if not Hesia_Sentinel_Util.File_Exists (Modules_Path) then
         return Modules_Path;
      end if;

      Open (F, In_File, Modules_Path);
      while not End_Of_File (F) loop
         declare
            Line : constant String := Hesia_Sentinel_Util.Trim (Get_Line (F));
         begin
            if Line'Length = 0 then
               goto Continue;
            end if;
            if Line (Line'First) = '#' or else Line (Line'First) = ';' then
               goto Continue;
            end if;
            if not Hesia_Sentinel_Util.File_Exists (Line) then
               if not First then
                  Append (Missing, ",");
               end if;
               Append (Missing, Line);
               First := False;
            end if;
            <<Continue>>
            null;
         end;
      end loop;
      Close (F);
      return To_String (Missing);
   exception
      when others =>
         return Modules_Path;
   end Missing_Modules;
end Hesia_Sentinel_Config;
