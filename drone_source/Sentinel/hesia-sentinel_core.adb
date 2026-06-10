with Ada.Text_IO;
with Hesia_Sentinel_Config;
with Hesia_Sentinel_Hash;
with Hesia_Sentinel_HW;
with Hesia_Sentinel_Util;

package body Hesia_Sentinel_Core is
   function Get_Modules_Path (Path : String) return String is
   begin
      return Hesia_Sentinel_Config.Get_Value (Path, "modules_required");
   end Get_Modules_Path;

   function Check return Integer is
      Conf_Path : constant String := "/etc/hesia/sentinel/allowlist.conf";
      Status    : Integer := 0;
      Cpu_Id       : constant String := Hesia_Sentinel_HW.Get_Cpu_Id;
      Storage_Id   : constant String := Hesia_Sentinel_HW.Get_Storage_Id;
      Exp_Cpu      : constant String := Hesia_Sentinel_Config.Get_Value (Conf_Path, "cpu_hash");
      Exp_Storage  : constant String := Hesia_Sentinel_Config.Get_Value (Conf_Path, "storage_hash");
      Modules_Path : constant String := Get_Modules_Path (Conf_Path);
   begin
      if not Hesia_Sentinel_Util.File_Exists (Conf_Path) then
         Ada.Text_IO.Put_Line ("[SENTINEL] allowlist missing");
         return 10;
      end if;

      if Cpu_Id'Length = 0 or else Storage_Id'Length = 0 then
         Ada.Text_IO.Put_Line ("[SENTINEL] hardware id missing");
         return 10;
      end if;

      if Exp_Cpu'Length = 0 or else Exp_Storage'Length = 0 then
         Ada.Text_IO.Put_Line ("[SENTINEL] allowlist missing required keys");
         return 10;
      end if;

      declare
         Cpu_Hash     : constant String := Hesia_Sentinel_Hash.Sha256_Hex (Cpu_Id);
         Storage_Hash : constant String := Hesia_Sentinel_Hash.Sha256_Hex (Storage_Id);
      begin
         if Hesia_Sentinel_Util.To_Lower (Exp_Cpu) /=
            Hesia_Sentinel_Util.To_Lower (Cpu_Hash)
         then
            Ada.Text_IO.Put_Line ("[SENTINEL] cpu_hash mismatch");
            Ada.Text_IO.Put_Line ("[SENTINEL] expected_cpu_hash=" & Exp_Cpu);
            Ada.Text_IO.Put_Line ("[SENTINEL] actual_cpu_hash=" & Cpu_Hash);
            Status := 2;
         end if;

         if Hesia_Sentinel_Util.To_Lower (Exp_Storage) /=
            Hesia_Sentinel_Util.To_Lower (Storage_Hash)
         then
            Ada.Text_IO.Put_Line ("[SENTINEL] storage_hash mismatch");
            Ada.Text_IO.Put_Line ("[SENTINEL] expected_storage_hash=" & Exp_Storage);
            Ada.Text_IO.Put_Line ("[SENTINEL] actual_storage_hash=" & Storage_Hash);
            Status := 2;
         end if;

         if Status = 0 then
            Ada.Text_IO.Put_Line ("[SENTINEL] ok");
            Ada.Text_IO.Put_Line ("[SENTINEL] cpu_hash=" & Cpu_Hash);
            Ada.Text_IO.Put_Line ("[SENTINEL] storage_hash=" & Storage_Hash);
         end if;
      end;

      if Modules_Path'Length > 0 then
         declare
            Missing : constant String := Hesia_Sentinel_Config.Missing_Modules (Modules_Path);
         begin
            if Missing'Length > 0 then
               Ada.Text_IO.Put_Line ("[SENTINEL] missing modules: " & Missing);
               Status := 3;
            end if;
         end;
      end if;

      return Status;
   end Check;
end Hesia_Sentinel_Core;
