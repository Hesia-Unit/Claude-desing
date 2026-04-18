with Ada.Strings.Unbounded;

package Hesia_Sentinel_Util is
   function Trim (S : String) return String;
   function Starts_With (S, Prefix : String) return Boolean;
   function To_Lower (S : String) return String;

   function Read_Text_File (Path : String; Max : Natural := 4096) return String;
   function Read_Bytes_File (Path : String; Max : Natural := 4096) return String;
   function File_Exists (Path : String) return Boolean;
end Hesia_Sentinel_Util;
