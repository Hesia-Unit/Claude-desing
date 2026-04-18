with Ada.Characters.Handling;
with Ada.Strings.Fixed;
with Ada.Strings.Unbounded;
with Ada.Text_IO;
with Hesia_Sentinel_Util;

package body Hesia_Sentinel_HW is
   use Ada.Strings.Unbounded;
   function Normalize_Id (S : String) return String is
      use Ada.Characters.Handling;
      use Ada.Strings.Fixed;
      Tmp : String (1 .. S'Length);
      Len : Natural := 0;
   begin
      for I in S'Range loop
         if S (I) /= Character'Val (0) then
            Len := Len + 1;
            Tmp (Len) := S (I);
         end if;
      end loop;
      if Len = 0 then
         return "";
      end if;
      return Trim (Tmp (1 .. Len), Ada.Strings.Both);
   end Normalize_Id;

   function Value_After_Colon (Line : String) return String is
      use Ada.Strings.Fixed;
      Pos : Natural := Index (Line, ":");
   begin
      if Pos = 0 or else Pos = Line'Last then
         return "";
      end if;
      return Trim (Line (Pos + 1 .. Line'Last), Ada.Strings.Both);
   end Value_After_Colon;

   function Read_First_From (Path : String) return String is
      Raw : constant String := Hesia_Sentinel_Util.Read_Bytes_File (Path, 1024);
   begin
      return Normalize_Id (Raw);
   end Read_First_From;

   function Get_Cpu_Id return String is
      type UArr is array (Positive range <>) of Unbounded_String;
      Candidates : constant UArr := (
         To_Unbounded_String ("/sys/firmware/devicetree/base/serial-number"),
         To_Unbounded_String ("/proc/device-tree/serial-number"),
         To_Unbounded_String ("/sys/devices/soc0/serial_number")
      );
   begin
      for I in Candidates'Range loop
         declare
            Path : constant String := To_String (Candidates (I));
         begin
            if Hesia_Sentinel_Util.File_Exists (Path) then
               declare
                  Id : constant String := Read_First_From (Path);
               begin
                  if Id'Length > 0 then
                     return Id;
                  end if;
               end;
            end if;
         end;
      end loop;
      return "";
   end Get_Cpu_Id;

   function Strip_Trailing_Digits (S : String) return String is
      I : Integer := S'Last;
   begin
      while I >= S'First and then S (I) in '0' .. '9' loop
         I := I - 1;
      end loop;
      if I < S'First then
         return S;
      end if;
      return S (S'First .. I);
   end Strip_Trailing_Digits;

   function Base_Block_Device (Dev : String) return String is
      P    : Integer := 0;
   begin
      if not Hesia_Sentinel_Util.Starts_With (Dev, "/dev/") then
         return "";
      end if;
      declare
         Name : constant String := Dev (Dev'First + 5 .. Dev'Last);
      begin
         if Hesia_Sentinel_Util.Starts_With (Name, "nvme") then
            -- nvme0n1p1 -> nvme0n1
            P := Name'Last;
            while P >= Name'First and then Name (P) in '0' .. '9' loop
               P := P - 1;
            end loop;
            if P >= Name'First and then Name (P) = 'p' then
               return Name (Name'First .. P - 1);
            end if;
            return Name;
         elsif Hesia_Sentinel_Util.Starts_With (Name, "mmcblk") then
            -- mmcblk0p2 -> mmcblk0, mmcblk0 -> mmcblk0
            P := Name'Last;
            while P >= Name'First and then Name (P) in '0' .. '9' loop
               P := P - 1;
            end loop;
            if P >= Name'First and then Name (P) = 'p' then
               return Name (Name'First .. P - 1);
            end if;
            return Name;
         else
            -- sda1 -> sda
            return Strip_Trailing_Digits (Name);
         end if;
      end;
   end Base_Block_Device;

   function Get_Root_Device return String is
      use Ada.Text_IO;
      F    : File_Type;
   begin
      Open (F, In_File, "/proc/self/mounts");
      while not End_Of_File (F) loop
         declare
            Line : constant String := Get_Line (F);
            Pos  : Natural := Ada.Strings.Fixed.Index (Line, " ");
         begin
            if Pos = 0 then
               goto Continue;
            end if;
            declare
               Dev  : constant String := Line (Line'First .. Pos - 1);
               Rest : constant String := Line (Pos + 1 .. Line'Last);
               Pos2 : Natural := Ada.Strings.Fixed.Index (Rest, " ");
            begin
               if Pos2 = 0 then
                  goto Continue;
               end if;
               declare
                  Mnt : constant String := Rest (Rest'First .. Pos2 - 1);
               begin
                  if Mnt = "/" then
                     Close (F);
                     return Dev;
                  end if;
               end;
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
   end Get_Root_Device;

   function Get_Storage_Id return String is
      Root_Dev : constant String := Get_Root_Device;
      Base     : constant String := Base_Block_Device (Root_Dev);
   begin
      if Base /= "" then
         declare
            type UArr is array (Positive range <>) of Unbounded_String;
            Candidates : constant UArr := (
               To_Unbounded_String ("/sys/block/" & Base & "/device/cid"),
               To_Unbounded_String ("/sys/block/" & Base & "/device/serial"),
               To_Unbounded_String ("/sys/block/" & Base & "/serial"),
               To_Unbounded_String ("/sys/block/" & Base & "/device/eui"),
               To_Unbounded_String ("/sys/block/" & Base & "/device/uuid"),
               To_Unbounded_String ("/sys/block/" & Base & "/device/wwid"),
               To_Unbounded_String ("/sys/block/" & Base & "/wwid")
            );
         begin
            for I in Candidates'Range loop
               declare
                  Path : constant String := To_String (Candidates (I));
               begin
                  if Hesia_Sentinel_Util.File_Exists (Path) then
                     declare
                        Id : constant String := Read_First_From (Path);
                     begin
                        if Id'Length > 0 then
                           return Id;
                        end if;
                     end;
                  end if;
               end;
            end loop;
         end;
      end if;
      return "";
   end Get_Storage_Id;
end Hesia_Sentinel_HW;
